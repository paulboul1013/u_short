# URL Shortener Context

This context describes the small set of domain concepts used by the local URL shortening service.

## Language

**Original URL**:
An absolute web address submitted to the service as the redirect destination.
_Avoid_: Long URL, target, link

**URL Record**:
A persisted association between a numeric identifier, an Original URL, and its creation time. Repeated Original URLs belong to distinct URL Records.
_Avoid_: Link record, mapping

**Short Code**:
The Base62 representation of a URL Record's numeric identifier. Creating another record for the same Original URL creates another Short Code.
_Avoid_: Slug, token, alias

**Short URL**:
The service's base URL combined with a Short Code. Visiting it redirects to the corresponding Original URL.
_Avoid_: Shortened link, alias URL
