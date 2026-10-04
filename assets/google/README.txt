Bundled trusted CA store
=======================
Source: https://curl.se/ca/cacert.pem
Extraction/source details: https://curl.se/docs/caextract.html
Mozilla source snapshot: 2026-09-25 03:12:01 GMT (121 certificates).
Downloaded and compared with curl's published cacert.pem.sha256 on 2026-10-04.
SHA256 of the complete bundled file:
a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505

This is curl's unmodified PEM conversion of Mozilla's trust store, licensed
under MPL 2.0 (included as MPL-2.0.txt). The upstream source location is also
recorded in the PEM header. Update deliberately from the same HTTPS source,
check its published checksum, and repeat TLS checks before distributing.
There is no runtime download or insecure fallback.

JSON parser: cJSON 1.7.19, unmodified cJSON.c / cJSON.h from
https://github.com/DaveGamble/cJSON/tree/v1.7.19
MIT license: included as cJSON-LICENSE.txt (also docs/cJSON-LICENSE).
Existing Apollo credits and project license remain unchanged.
