#ifndef AWS_CLIENT_CREDENTIAL_KEYS_H
#define AWS_CLIENT_CREDENTIAL_KEYS_H

#include <stdint.h>

/*
 * PEM-encoded client certificate.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----"
 */
#define keyCLIENT_CERTIFICATE_PEM \
"-----BEGIN CERTIFICATE-----\n"\
"MIIDWTCCAkGgAwIBAgIUFX0fR2SKN2Z9zL084bTTure/ewgwDQYJKoZIhvcNAQEL\n"\
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n"\
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTE5MDgyMTE4MDAx\n"\
"MloXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n"\
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAOMdc5rgslpw7z9gAiEy\n"\
"YxJFvnfjxf4STHFGUn0VxoCmTpZIIT7K85/lmzJeshy/JP+6cUyszqMxjRA3bEHi\n"\
"0lpj7zAX3ztH9PjL4kduLbpGcfpiNUlVVoXlCxrcoe00y/p0cdXK4alvJyekHcCH\n"\
"8FQHgHviyngeYWFpNn0rArQYvp3Bib13Gu/c8wvY1+/4nfOxoOwIAF5H+ROzmhlt\n"\
"9qdfNkaDdZFmPmue6SAsEFHn9/sH0SGlXvdTWY3p3feKlfcye/tSrO6KK8T16vhi\n"\
"1l+7DHMDLKz+i0dHCDc+gYaFVuMwRVS2fT9WaSfyyMVJ0j83no4sxrxKZzh4oBuP\n"\
"1FUCAwEAAaNgMF4wHwYDVR0jBBgwFoAUr9t/82RLJUpZxx5BHkgAzNpvzjMwHQYD\n"\
"VR0OBBYEFEadQPfMuecVb5WQD6SKKq58SSMHMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n"\
"AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQB4rJHT8FW7xtUBYqHeWCZxYc8l\n"\
"g1xUzVDemx/gwGsiExVo+jHDcP3dboITRHgoKU83ZYh219J1TUxLV4j0kh4G9Dmp\n"\
"nWbF1LJtDlbMBsYJv1Z/hFIz48ptmG/CSCnXDQVpBGjQLk2Y5dGQeEELVhC3udaE\n"\
"1GGhL2nUuJZPLLXMDdE3ret7f72AHfZB3qAcUE1kd5MbT0IXhV6deiU/wOtmdcBl\n"\
"D9CwCPlWWErR8ERHRcK47HuEE4eP7/dbeU8WZV/+1yMknBYPZDWvDB62yWEaiPNE\n"\
"UdSvwzcYdnPv45o1zhh3A91mMZVXYmyQ9EQYaqG0F5WURZQqMynzjnABZpJH\n"\
"-----END CERTIFICATE-----"

/*
 * PEM-encoded client private key.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN RSA PRIVATE KEY-----\n"\
 * "...base64 data...\n"\
 * "-----END RSA PRIVATE KEY-----"
 */
#define keyCLIENT_PRIVATE_KEY_PEM \
"-----BEGIN RSA PRIVATE KEY-----\n"\
"MIIEpAIBAAKCAQEA4x1zmuCyWnDvP2ACITJjEkW+d+PF/hJMcUZSfRXGgKZOlkgh\n"\
"Psrzn+WbMl6yHL8k/7pxTKzOozGNEDdsQeLSWmPvMBffO0f0+MviR24tukZx+mI1\n"\
"SVVWheULGtyh7TTL+nRx1crhqW8nJ6QdwIfwVAeAe+LKeB5hYWk2fSsCtBi+ncGJ\n"\
"vXca79zzC9jX7/id87Gg7AgAXkf5E7OaGW32p182RoN1kWY+a57pICwQUef3+wfR\n"\
"IaVe91NZjend94qV9zJ7+1Ks7oorxPXq+GLWX7sMcwMsrP6LR0cINz6BhoVW4zBF\n"\
"VLZ9P1ZpJ/LIxUnSPzeejizGvEpnOHigG4/UVQIDAQABAoIBAQDBx+lRppLHFEEu\n"\
"g86NYJ6jgpqnVNMkm5xZsjTDwYajUkMPeu/S0fvY3GH1MlMqr/TztbLiRzfwimDg\n"\
"C0n1VYFB2q7FDDlOLXFZryyc4edmXqD1kEIDFitDB1DYVJRc/oVkZ2KRlg/BL7Pq\n"\
"4N753YMzT0tTaQh0QQe5w7nYRpCB9FbfOKjnWzeFzAV6ETdTmbInpdQ19Uq7sY77\n"\
"c4XaBwBAH5cRBeUGYUQOJl9LTpMKxqN5VItk3z2QHn6KBFjoBG4vJqMffVMxmdYz\n"\
"IQjjv4ILEy2ZSVCnOCkdAgfVs8edtdQtNwd5M/uPKt52DVXGqThoEcNREAek1xYR\n"\
"f2ecsU2BAoGBAP8U/lo1h0mru6K4mLey6btYULmKhygKwOlyPOLGVIwOyhXp8xry\n"\
"c+/98lIya7mHobCFAn0e0GoUhiOzrdcNeCINfIzsNMzlxcq0vIkmbulnguJZq8ab\n"\
"X2pTSmYkEGumBHnurZChl5kujoJcGSightfrMgYxzYeVnGlP6x4Ld05FAoGBAOPu\n"\
"sS8St72OpVm1wBsWxWhDke1Xsv/36J4ywx6CNFD0DekQaCYLWvg5kF0ETcY034ty\n"\
"SPP5QrqjACAV8DyDcUreCF3NsXGKlszYtD2j4G7Uin6XQ2kt9N3QYc5UECXIorIu\n"\
"ciXvJHkDr3lt/Xt2r/SU0ekt7OG3ci3PsifPIxbRAoGAFKvf3PGfkBHRt+MbxS9D\n"\
"j8IdcJvQTido8MIoOvx1l5APQm1eHR3u1VEQFScu4a8jJEnzJK6dlyson7YM7XOS\n"\
"+7d6E3WE5eHtAHtbGAY8UL/ptGwt5n9q4RP04IglqIOgszzrZeAih/Bk7h2GkGtJ\n"\
"4i+WMzhP/p/aOBKY7CtjtL0CgYADcI2x9SlarfVm1ixQ/FX8TdC88S7dWANlp8R0\n"\
"CGj7s8Ml+j5oHJHB3zSDgtYdoJyjARWCwQc1w9HzqgEXGLCz/YfIyprb6Mh/zOFj\n"\
"cL6pTYHNiUDWLIBefI+NSo131IBIChVk5yf4v4p9XaCOpWrgWQKV70B844TQlirN\n"\
"GP4hkQKBgQDkudguzrqd1WAfWdYSY3ieybatZyJL6O/VW1ssPc2R7L28s/kH+/NQ\n"\
"d4QuWQ4t2jRGX0TA+/8eEPiogAGBINWCLItG9vzVm9iPj9lAcqDm9jaIQP34iLZ3\n"\
"YX9cSuTezJVDT6qhNqj5EF/m7B69qTnuzO7IchZmQTJ+JAM266ReYQ==\n"\
"-----END RSA PRIVATE KEY-----"

/*
 * PEM-encoded Just-in-Time Registration (JITR) certificate (optional).
 *
 * If used, must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----"
 */
#define keyJITR_DEVICE_CERTIFICATE_AUTHORITY_PEM  ""


#endif /* AWS_CLIENT_CREDENTIAL_KEYS_H */
