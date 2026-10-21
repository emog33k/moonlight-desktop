Test-only certificates and keys (self-signed, never used outside unit tests).
Regenerate: openssl req -x509 -newkey rsa:2048 -nodes -keyout <n>.key -out <n>.crt -days 7300 -subj "/CN=LocalRDP test <n>"
