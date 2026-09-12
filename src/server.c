#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include "http.h"
#include "router.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SERVER_PORT 8080U
#define SERVER_BACKLOG 16
#define SERVER_REQUEST_CAPACITY (HTTP_MAX_HEADER_BYTES + HTTP_MAX_BODY_BYTES)
#define SERVER_ACCEPT_POLL_MS 100
#define SERVER_REQUEST_DEADLINE_MS 2000

static volatile sig_atomic_t stop_requested = 0;

static void request_stop(int signal_number)
{
    (void)signal_number;
    stop_requested = 1;
}

static int send_all(int socket_fd, const char *bytes, size_t length)
{
    size_t sent = 0U;

    while (sent < length) {
        ssize_t result;

#ifdef MSG_NOSIGNAL
        result = send(socket_fd, bytes + sent, length - sent, MSG_NOSIGNAL);
#else
        result = send(socket_fd, bytes + sent, length - sent, 0);
#endif
        if (result > 0) {
            sent += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

static void send_response(int client_fd, const http_response_t *response)
{
    char bytes[HTTP_MAX_RESPONSE_BYTES];
    size_t length = 0U;

    if (!http_serialize_response(response, bytes, sizeof(bytes), &length)) {
        (void)fprintf(stderr, "failed to serialize HTTP response\n");
        return;
    }
    if (send_all(client_fd, bytes, length) != 0) {
        (void)fprintf(stderr, "failed to send HTTP response: %s\n",
                      strerror(errno));
    }
}

static void send_parse_error(int client_fd, int status_code,
                             const char *message)
{
    http_response_t response;

    if (!http_response_init(&response, status_code, "text/plain", message,
                            strlen(message))) {
        (void)fprintf(stderr, "failed to initialize HTTP error response\n");
        return;
    }
    send_response(client_fd, &response);
}

static int wait_for_input(int socket_fd, int timeout_ms)
{
    struct pollfd descriptor;

    descriptor.fd = socket_fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    while (!stop_requested) {
        int result = poll(&descriptor, 1, timeout_ms);

        if (result > 0) {
            if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                return 1;
            }
            return -1;
        }
        if (result == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
    return -2;
}

static int monotonic_milliseconds(int64_t *milliseconds)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        return -1;
    }
    *milliseconds = (int64_t)current.tv_sec * INT64_C(1000) +
                    (int64_t)(current.tv_nsec / 1000000L);
    return 0;
}

static int wait_for_input_until(int socket_fd, int64_t deadline)
{
    while (!stop_requested) {
        int64_t current;
        int64_t remaining;
        int timeout_ms;
        int result;
        struct pollfd descriptor;

        if (monotonic_milliseconds(&current) != 0) {
            return -1;
        }
        remaining = deadline - current;
        if (remaining <= 0) {
            return 0;
        }
        timeout_ms = remaining > INT_MAX ? INT_MAX : (int)remaining;
        descriptor.fd = socket_fd;
        descriptor.events = POLLIN;
        descriptor.revents = 0;
        result = poll(&descriptor, 1, timeout_ms);
        if (result > 0) {
            if ((descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                return 1;
            }
            return -1;
        }
        if (result == 0) {
            return 0;
        }
        if (errno != EINTR) {
            return -1;
        }
    }
    return -2;
}

static void handle_client(int client_fd, shortener_t *shortener)
{
    char bytes[SERVER_REQUEST_CAPACITY];
    size_t received = 0U;
    int64_t started;
    int64_t deadline;

    if (monotonic_milliseconds(&started) != 0) {
        (void)fprintf(stderr, "failed to read monotonic clock: %s\n",
                      strerror(errno));
        return;
    }
    deadline = started + SERVER_REQUEST_DEADLINE_MS;

    while (!stop_requested) {
        http_request_t request;
        http_parse_status_t parse_status =
            http_parse_request(bytes, received, &request);

        if (parse_status == HTTP_PARSE_OK) {
            http_response_t response;

            router_handle(&request, shortener, &response);
            send_response(client_fd, &response);
            return;
        }
        if (parse_status == HTTP_PARSE_UNSUPPORTED_TRANSFER) {
            send_parse_error(client_fd, 501, "Not Implemented");
            return;
        }
        if (parse_status != HTTP_PARSE_INCOMPLETE) {
            send_parse_error(client_fd, 400, "Bad Request");
            return;
        }
        if (received == sizeof(bytes)) {
            send_parse_error(client_fd, 400, "Bad Request");
            return;
        }

        {
            int readiness = wait_for_input_until(client_fd, deadline);
            ssize_t result;

            if (readiness == -2) {
                return;
            }
            if (readiness == 0) {
                send_parse_error(client_fd, 400, "Bad Request");
                return;
            }
            if (readiness < 0) {
                (void)fprintf(stderr, "failed while waiting for request: %s\n",
                              strerror(errno));
                return;
            }
            result = recv(client_fd, bytes + received,
                          sizeof(bytes) - received, 0);

            if (result > 0) {
                received += (size_t)result;
                continue;
            }
            if (result == 0) {
                send_parse_error(client_fd, 400, "Bad Request");
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            (void)fprintf(stderr, "failed to read HTTP request: %s\n",
                          strerror(errno));
            return;
        }
    }
}

static int open_listener(void)
{
    struct sockaddr_in address;
    int listener_fd;
    int reuse_address = 1;

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) {
        (void)fprintf(stderr, "failed to create listening socket: %s\n",
                      strerror(errno));
        return -1;
    }
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                   sizeof(reuse_address)) != 0) {
        (void)fprintf(stderr, "failed to configure listening socket: %s\n",
                      strerror(errno));
        (void)close(listener_fd);
        return -1;
    }

    (void)memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener_fd, (const struct sockaddr *)&address,
             sizeof(address)) != 0) {
        (void)fprintf(stderr, "failed to bind 127.0.0.1:8080: %s\n",
                      strerror(errno));
        (void)close(listener_fd);
        return -1;
    }
    if (listen(listener_fd, SERVER_BACKLOG) != 0) {
        (void)fprintf(stderr, "failed to listen on 127.0.0.1:8080: %s\n",
                      strerror(errno));
        (void)close(listener_fd);
        return -1;
    }

    return listener_fd;
}

int server_run(shortener_t *shortener)
{
    struct sigaction action;
    struct sigaction old_interrupt;
    struct sigaction old_terminate;
    struct sigaction ignore_pipe;
    struct sigaction old_pipe;
    int listener_fd;
    int result = 0;

    if (shortener == NULL) {
        (void)fprintf(stderr, "cannot run server without a shortener\n");
        return 1;
    }

    (void)memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    (void)sigemptyset(&action.sa_mask);
    stop_requested = 0;
    if (sigaction(SIGINT, &action, &old_interrupt) != 0) {
        (void)fprintf(stderr, "failed to install SIGINT handler: %s\n",
                      strerror(errno));
        return 1;
    }
    if (sigaction(SIGTERM, &action, &old_terminate) != 0) {
        (void)fprintf(stderr, "failed to install SIGTERM handler: %s\n",
                      strerror(errno));
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        return 1;
    }
    (void)memset(&ignore_pipe, 0, sizeof(ignore_pipe));
    ignore_pipe.sa_handler = SIG_IGN;
    (void)sigemptyset(&ignore_pipe.sa_mask);
    if (sigaction(SIGPIPE, &ignore_pipe, &old_pipe) != 0) {
        (void)fprintf(stderr, "failed to ignore SIGPIPE: %s\n",
                      strerror(errno));
        (void)sigaction(SIGTERM, &old_terminate, NULL);
        (void)sigaction(SIGINT, &old_interrupt, NULL);
        return 1;
    }

    listener_fd = open_listener();
    if (listener_fd < 0) {
        result = 1;
    }

    while (listener_fd >= 0 && !stop_requested) {
        int readiness = wait_for_input(listener_fd, SERVER_ACCEPT_POLL_MS);

        if (readiness == -2) {
            break;
        }
        if (readiness == 0) {
            continue;
        }
        if (readiness < 0) {
            (void)fprintf(stderr, "failed while waiting for connection: %s\n",
                          strerror(errno));
            result = 1;
            break;
        }

        int client_fd = accept(listener_fd, NULL, NULL);

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            (void)fprintf(stderr, "failed to accept connection: %s\n",
                          strerror(errno));
            result = 1;
            break;
        }
        handle_client(client_fd, shortener);
        if (close(client_fd) != 0) {
            (void)fprintf(stderr, "failed to close client socket: %s\n",
                          strerror(errno));
        }
    }

    if (listener_fd >= 0 && close(listener_fd) != 0) {
        (void)fprintf(stderr, "failed to close listening socket: %s\n",
                      strerror(errno));
        result = 1;
    }
    if (sigaction(SIGINT, &old_interrupt, NULL) != 0) {
        (void)fprintf(stderr, "failed to restore SIGINT handler: %s\n",
                      strerror(errno));
        result = 1;
    }
    if (sigaction(SIGTERM, &old_terminate, NULL) != 0) {
        (void)fprintf(stderr, "failed to restore SIGTERM handler: %s\n",
                      strerror(errno));
        result = 1;
    }
    if (sigaction(SIGPIPE, &old_pipe, NULL) != 0) {
        (void)fprintf(stderr, "failed to restore SIGPIPE handler: %s\n",
                      strerror(errno));
        result = 1;
    }

    return result;
}
