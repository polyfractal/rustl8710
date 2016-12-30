#include <polarssl/ssl.h>
#include <polarssl/memory.h>

//#define SSL_VERIFY_CLIENT
//#define SSL_VERIFY_SERVER

#ifdef SSL_VERIFY_CLIENT
static x509_crt* _cli_crt = NULL;
static pk_context* _clikey_rsa = NULL;

static const char *test_client_key = \
"-----BEGIN RSA PRIVATE KEY-----\r\n" \
"MIICXgIBAAKBgQDKLbkPtV0uhoqkHxHl/sZlq5TrUqu6pScqGkMnEUDKIFR5QMNf\r\n" \
"qLgbGPwbreN4AkHQlvqnn/2Swz1uurUH4pxcGp54j7QmANXvd5hJtCMhPpDcPS6k\r\n" \
"ldlIJ8y3KoCoqAot6uo9IL/IKKk3aOQqeHKayIyjOOksjMkgeE8/gCpmFQIDAQAB\r\n" \
"AoGBAKoSBj+Bh83wXUWr4SmAxLGXwSCnHVBXRveyudRuPfsJcSXCZdbdHWml/cTm\r\n" \
"5Jb6BxUJO/avreW8GLxBkLD+XhnXlkw1RJ8FYZPXdzlNJzoYyVK0GZ/qyGacEEFt\r\n" \
"ekvGfBJIq+7ksKcJt5c9qARClOvauYLRGwubl64xD6PupSINAkEA+5C395h227nc\r\n" \
"5zF8s2rYBP78i5uS7hKqqVjGy8pcIFHiM/0ehzcN3V3gJXLjkAbXfvP0h/tm8eQG\r\n" \
"QUpJBY/YLwJBAM2+IOfTmEBxrpASUeN1Lx9yg0+Swyz8oz2a2blfFwbpCWBi18M2\r\n" \
"huo+YECeMggqBBYwgQ9J2ixpaj/e9+0pkPsCQQDztTWkFf4/y4WoLBcEseNoo6YB\r\n" \
"kcv7+/V9bdXZI8ewP+OGPhdPIxS5efJmFTFEHHy0Lp6dBf6rJB6zLcYkL0BdAkEA\r\n" \
"nGBqeknlavX9DBwgiZXD308WZyDRoBvVpzlPSwnvYp01N0FpZULIgLowRmz28iWd\r\n" \
"PZBYR9qGLUNiMnGyV1xEiQJAOdlBM4M9Xj2Z9inCdkgFkbIOSe5kvIPC24CjZyyG\r\n" \
"g3lK/YezoDmdD//OLoY81y6VdO5dwjm7P0wZB63EDRidHA==\r\n" \
"-----END RSA PRIVATE KEY-----\r\n";

static const char *test_client_cert = \
"-----BEGIN CERTIFICATE-----\r\n" \
"MIIC4DCCAkmgAwIBAgIBAjANBgkqhkiG9w0BAQsFADB7MQswCQYDVQQGEwJDTjEL\r\n" \
"MAkGA1UECAwCSlMxCzAJBgNVBAcMAlNaMRAwDgYDVQQKDAdSZWFsc2lsMRAwDgYD\r\n" \
"VQQLDAdSZWFsdGVrMRAwDgYDVQQDDAdSZWFsc2lsMRwwGgYJKoZIhvcNAQkBFg1h\r\n" \
"QHJlYWxzaWwuY29tMB4XDTE1MTIyMzA2NTI0MFoXDTE2MTIyMjA2NTI0MFowdDEL\r\n" \
"MAkGA1UEBhMCQ04xCzAJBgNVBAgMAkpTMRAwDgYDVQQKDAdSZWFsc2lsMRAwDgYD\r\n" \
"VQQLDAdSZWFsdGVrMRYwFAYDVQQDDA0xOTIuMTY4LjEuMTQxMRwwGgYJKoZIhvcN\r\n" \
"AQkBFg1jQHJlYWxzaWwuY29tMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDK\r\n" \
"LbkPtV0uhoqkHxHl/sZlq5TrUqu6pScqGkMnEUDKIFR5QMNfqLgbGPwbreN4AkHQ\r\n" \
"lvqnn/2Swz1uurUH4pxcGp54j7QmANXvd5hJtCMhPpDcPS6kldlIJ8y3KoCoqAot\r\n" \
"6uo9IL/IKKk3aOQqeHKayIyjOOksjMkgeE8/gCpmFQIDAQABo3sweTAJBgNVHRME\r\n" \
"AjAAMCwGCWCGSAGG+EIBDQQfFh1PcGVuU1NMIEdlbmVyYXRlZCBDZXJ0aWZpY2F0\r\n" \
"ZTAdBgNVHQ4EFgQUJLmwJNyKHCTEspNTPNpbPjXkjnQwHwYDVR0jBBgwFoAUAfLa\r\n" \
"cSF933h+3pYNcs36lvm7yEkwDQYJKoZIhvcNAQELBQADgYEAlo495gu94nMHFYx4\r\n" \
"+V7PjwGIqanqwLjsem9qvwJa/K1QoM4JxnqRXFUdSfZMhnlrMgPer4fDHpWAutWB\r\n" \
"X2Fiww+VVJSn8Go0seK8RQf8n/n3rJ5B3lef1Po2zHchELWhlFT6k5Won7gp64RN\r\n" \
"9PcwFFy0Va/bkJsot//kdZNKs/g=\r\n" \
"-----END CERTIFICATE-----\r\n";
#endif

#ifdef SSL_VERIFY_SERVER
static x509_crt* _ca_crt = NULL;

static const char *test_ca_cert = \
"-----BEGIN CERTIFICATE-----\r\n" \
"MIICxDCCAi2gAwIBAgIJANdeY8UOfqpBMA0GCSqGSIb3DQEBCwUAMHsxCzAJBgNV\r\n" \
"BAYTAkNOMQswCQYDVQQIDAJKUzELMAkGA1UEBwwCU1oxEDAOBgNVBAoMB1JlYWxz\r\n" \
"aWwxEDAOBgNVBAsMB1JlYWx0ZWsxEDAOBgNVBAMMB1JlYWxzaWwxHDAaBgkqhkiG\r\n" \
"9w0BCQEWDWFAcmVhbHNpbC5jb20wHhcNMTUxMjIzMDYzMDA1WhcNMTYxMjIyMDYz\r\n" \
"MDA1WjB7MQswCQYDVQQGEwJDTjELMAkGA1UECAwCSlMxCzAJBgNVBAcMAlNaMRAw\r\n" \
"DgYDVQQKDAdSZWFsc2lsMRAwDgYDVQQLDAdSZWFsdGVrMRAwDgYDVQQDDAdSZWFs\r\n" \
"c2lsMRwwGgYJKoZIhvcNAQkBFg1hQHJlYWxzaWwuY29tMIGfMA0GCSqGSIb3DQEB\r\n" \
"AQUAA4GNADCBiQKBgQCmfNpluJZP0Sla+MIYzRGA1rljK5VncuBKQiKBF4BdO73H\r\n" \
"OTUoT0ydR7x7lS2Ns1HQop2oldroJVBj38+pLci1i/3flkONCDfsWOzfcGZ9RItq\r\n" \
"Zf9eQI8CEZI5i0Fvi3mgaoqCXvutFBrtTQRNsKQD69SqxEWWPb1y+Fd2nONeawID\r\n" \
"AQABo1AwTjAdBgNVHQ4EFgQUAfLacSF933h+3pYNcs36lvm7yEkwHwYDVR0jBBgw\r\n" \
"FoAUAfLacSF933h+3pYNcs36lvm7yEkwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0B\r\n" \
"AQsFAAOBgQA6McwC1Vk4k/5Bh/sf9cfwSK9A0ecaIH0NizYoWpWRAsv7TDgj0PbO\r\n" \
"Qqxi/QhpuYezgRqKqAv7QYNSQa39X7opzSsdSGtTnId374PZZeCDqZpfcAbsNk5o\r\n" \
"6HLpJ27esFa/flTL0FtmO+AT2uiPMvRP0a4u4uuLQK2Jgm/CmzJ47w==\r\n" \
"-----END CERTIFICATE-----\r\n";

static int my_verify(void *data, x509_crt *crt, int depth, int *flags) 
{
	char buf[1024];
	((void) data);

	printf("Verify requested for (Depth %d):\n", depth);
	x509_crt_info(buf, sizeof(buf) - 1, "", crt);
	printf("%s", buf);

	if(((*flags) & BADCERT_EXPIRED) != 0)
		printf("server certificate has expired\n");

	if(((*flags) & BADCERT_REVOKED) != 0)
		printf("  ! server certificate has been revoked\n");

	if(((*flags) & BADCERT_CN_MISMATCH) != 0)
		printf("  ! CN mismatch\n");

	if(((*flags) & BADCERT_NOT_TRUSTED) != 0)
		printf("  ! self-signed or not signed by a trusted CA\n");

	if(((*flags) & BADCRL_NOT_TRUSTED) != 0)
		printf("  ! CRL not trusted\n");

	if(((*flags) & BADCRL_EXPIRED) != 0)
		printf("  ! CRL expired\n");

	if(((*flags) & BADCERT_OTHER) != 0)
		printf("  ! other (unknown) flag\n");

	if((*flags) == 0)
		printf("  Certificate verified without error flags\n");

	return(0);
}
#endif

int ssl_client_ext_init(void)
{
#ifdef SSL_VERIFY_CLIENT
	_cli_crt = polarssl_malloc(sizeof(x509_crt));
	
	if(_cli_crt)
		x509_crt_init(_cli_crt);
	else
		return -1;

	_clikey_rsa = polarssl_malloc(sizeof(pk_context));

	if(_clikey_rsa)
		pk_init(_clikey_rsa);
	else
		return -1;
#endif
#ifdef SSL_VERIFY_SERVER
	_ca_crt = polarssl_malloc(sizeof(x509_crt));

	if(_ca_crt)
		x509_crt_init(_ca_crt);
	else
		return -1;
#endif
	return 0;
}

void ssl_client_ext_free(void)
{
#ifdef SSL_VERIFY_CLIENT
	if(_cli_crt) {
		x509_crt_free(_cli_crt);
		polarssl_free(_cli_crt);
		_cli_crt = NULL;
	}

	if(_clikey_rsa) {
		pk_free(_clikey_rsa);
		polarssl_free(_clikey_rsa);
		_clikey_rsa = NULL;
	}
#endif	
#ifdef SSL_VERIFY_SERVER
	if(_ca_crt) {
		x509_crt_free(_ca_crt);
		polarssl_free(_ca_crt);
		_ca_crt = NULL;
	}
#endif
}

int ssl_client_ext_setup(ssl_context *ssl)
{
#ifdef SSL_VERIFY_CLIENT
	if(x509_crt_parse(_cli_crt, test_client_cert, strlen(test_client_cert)) != 0)
		return -1;

	if(pk_parse_key(_clikey_rsa, test_client_key, strlen(test_client_key), NULL, 0) != 0)
		return -1;

	ssl_set_own_cert(ssl, _cli_crt, _clikey_rsa);
#endif
#ifdef SSL_VERIFY_SERVER
	if(x509_crt_parse(_ca_crt, test_ca_cert, strlen(test_ca_cert)) != 0)
		return -1;

	ssl_set_ca_chain(ssl, _ca_crt, NULL, NULL);
	ssl_set_authmode(ssl, SSL_VERIFY_REQUIRED);
	ssl_set_verify(ssl, my_verify, NULL);
#endif
	return 0;
}
