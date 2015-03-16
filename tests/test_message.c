/* tests/test_configuration.c */

#include <check.h>
#include <stdlib.h>
#include "scep.h"

SCEP *handle;
BIO *scep_log;
PKCS7 *p7 = NULL;
SCEP_DATA *pkiMessage;
PKCS7 *p7_nosigcert = NULL; // no signer certificate on result PKCS#7
EVP_PKEY *dec_key;
X509 *dec_cert;

char *test_crt = "-----BEGIN CERTIFICATE-----\n"
"MIICLzCCAZgCCQDTeVgTQPW40zANBgkqhkiG9w0BAQUFADBbMQswCQYDVQQGEwJE\n"
"RTETMBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0\n"
"cyBQdHkgTHRkMRQwEgYDVQQDEwtmb28uYmFyLmNvbTAgFw0xNTAyMjYxMjAwMzla\n"
"GA8yMTE1MDIwMjEyMDAzOVowWzELMAkGA1UEBhMCREUxEzARBgNVBAgTClNvbWUt\n"
"U3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDEUMBIGA1UE\n"
"AxMLZm9vLmJhci5jb20wgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBANFEiTNr\n"
"xDGehD636meTlC2yAmINuZn7pU9CC4BudfdDAI2YdoB9h9YqRk773EYAveAfSMYg\n"
"/ySzMlzz+yb8skZwctrocJYGpgB4N0BpmkGt7VSK9qwT4mRXqL6G2Cvvifi4BBYP\n"
"Q4c5JvYP43cDd7/Yb7Hg3Do8tG16Zo6AXaFpAgMBAAEwDQYJKoZIhvcNAQEFBQAD\n"
"gYEAbUXoPS+AhHuO7T7KRdgwJDLyr15dwUplGwtZT+MoOnnDMRWv/0VG4QUbBwvP\n"
"5Jrrk/lRHKajXLmzrqaoiadGzj6vCOh+zuf/KAOhQjvYtZyL0b727W1Sf2i7Cij+\n"
"ublOOHR0hldn/XqR7hKfZ/uIPnznQeKkVGjrEs223vtf7cI=\n"
"-----END CERTIFICATE-----\n";

char *test_key = "-----BEGIN RSA PRIVATE KEY-----\n"
"MIICXgIBAAKBgQDRRIkza8QxnoQ+t+pnk5QtsgJiDbmZ+6VPQguAbnX3QwCNmHaA\n"
"fYfWKkZO+9xGAL3gH0jGIP8kszJc8/sm/LJGcHLa6HCWBqYAeDdAaZpBre1Uivas\n"
"E+JkV6i+htgr74n4uAQWD0OHOSb2D+N3A3e/2G+x4Nw6PLRtemaOgF2haQIDAQAB\n"
"AoGBAM9w9eRwLmLVdNhLLeSQqXGGpNAYNOTMTDk+CfK9DNkXpQO3n7iNN0r4Swve\n"
"pKML9yylNlmYufLiY8k63brvAbP/Tfg2cbzg47fv+kacqYgaH6aoII++UEAoF+pM\n"
"HgdINRIRn+wknsNxdxE+YEJW/+XfhHiwD31RKBFOYw0NL3PRAkEA+pTwf1CfXBLP\n"
"Ujap10y883PQpX+lLyFzMT2BEu5C1WfUSjHiyzZyb6utYZ9U1PlTvaUXSJ3guNcl\n"
"VVvwjll/rQJBANXK6H9xy959Y7EKfxT41BDHQoXfmEIcLSz1wgWSeKwgbFF4+n3g\n"
"JoHG1n4hQ7D6OV41oh18XXYFBE9Ienyw8C0CQEHs0WENev+kSzsb+o8UN1ntjGUe\n"
"Mf02VbIMtlqeqKKwkF98xGgmSPEsP49BdfYaKnfoaTnHn4nBwKa2a5Fn5nkCQQCr\n"
"nApwcmnRGBlzvRcxQGMJbMjrKQXQ20kv871gN6iBki0gYNnBPLHsLi1yZUUuxExU\n"
"YPzWakOjPnetJGKdwHGpAkEAnMDbIjYpg9WYtx4l5q8R8u1USf8ndybDQehite7W\n"
"nzpG25y4ERn1b0M8TJ0xK0y2b8pMWBYlavUYkCYCfWOAsw==\n"
"-----END RSA PRIVATE KEY-----";

char *test_new_key = "-----BEGIN RSA PRIVATE KEY-----\n"
"MIICXAIBAAKBgQCnCz5qi3kW8avPCPhmKOUwSRpCcqOi0RH3tGburtCoHl56nhL3\n"
"X1Xuv+3e6HWS74IOWbwuZXADdSWswFMefJuh6D4tRACzvgbOuXaxxopj9PYnieNu\n"
"nATNl1O1fy1QG3uJiy+QuQe3/xfIIwIVtvsx5ckMfRHk4g4lsOJwLofIvwIDAQAB\n"
"AoGAGt9dMCi11zITsJ/BzjWAAU+RUOU+W+AEYvP7pyQqXxFbo6AwbulAWsM3kieV\n"
"Woj7RDG9az1YUsYSxILAHGRxaMzpMtJISEECUqlmDYU+/vinU/vYp0a2oAuqFg4G\n"
"8nSoOQ2aTG5owNNcSrK7FbEcI2XdKZNNHM+82iYv7DA4tBECQQDYJLdeudpBhgiE\n"
"u6XaRfvlOeRWK7kfgIloz23qjfbgpDkVO40gIOMxUfU7ut19PuwJ5yJJG4mYCCbP\n"
"wR9Bu1snAkEAxdi7hfgj4Lkdh3C/Qki5K5Q7KR2K6Xhfzpn+fY4SmsLHd/v6QYhF\n"
"+igQv3Y357dz67+9dxWBzaMsMBFOM7QEqQJBAJadXzofADvQjncP246yXclqAfca\n"
"GLIe+6GRieJ8cqAvT6fAC6Nrx2VC20R3/oecJRbxfS68hbDvXTxAMuu3BtkCQGdP\n"
"q2xjjOiWAZNuDpFgREE7YEEyCg2sK+tIgpmxjIl/2IUQ8TczH8dnEIfKBZtcMo4S\n"
"S69ZbbSh1jsrbjiVcjECQAlyT5MO1eWxksYaW4aFx8w+QO9vxQh0vgkI1fBArbzt\n"
"sj4kcSMpE9Tn8CeAhi1d0Qwayo8QO1TPbIgay02syMo=\n"
"-----END RSA PRIVATE KEY-----";

char *test_new_csr = "-----BEGIN CERTIFICATE REQUEST-----\n"
"MIIBtTCCAR4CAQAwVzELMAkGA1UEBhMCQVUxEzARBgNVBAgTClNvbWUtU3RhdGUx\n"
"ITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDEQMA4GA1UEAxMHZm9v\n"
"LmJhcjCBnzANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEApws+aot5FvGrzwj4Zijl\n"
"MEkaQnKjotER97Rm7q7QqB5eep4S919V7r/t3uh1ku+CDlm8LmVwA3UlrMBTHnyb\n"
"oeg+LUQAs74Gzrl2scaKY/T2J4njbpwEzZdTtX8tUBt7iYsvkLkHt/8XyCMCFbb7\n"
"MeXJDH0R5OIOJbDicC6HyL8CAwEAAaAeMBwGCSqGSIb3DQEJBzEPEw1GT09CQVJU\n"
"RVNUUFdEMA0GCSqGSIb3DQEBBQUAA4GBACHwu5U6KNAsgFkmgU6DNBQXriPwRvvn\n"
"uGCzClbjbwGnoi9XCtgepO6I6AbDokjpuuU8/JEGAqKwtRzOsvGJyq4tphAPf/89\n"
"/H+xoHva5tgIGv9zUQSj/6Q0B7TEUKLfVC4H0K9wde+5g13l82EzXXrsCjnyB3S7\n"
"SLYGjIEJ2RwX\n"
"-----END CERTIFICATE REQUEST-----";

char *test_server_cert = "-----BEGIN CERTIFICATE-----\n"
"MIICLzCCAZgCCQDTeVgTQPW40zANBgkqhkiG9w0BAQUFADBbMQswCQYDVQQGEwJE\n"
"RTETMBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0\n"
"cyBQdHkgTHRkMRQwEgYDVQQDEwtmb28uYmFyLmNvbTAgFw0xNTAyMjYxMjAwMzla\n"
"GA8yMTE1MDIwMjEyMDAzOVowWzELMAkGA1UEBhMCREUxEzARBgNVBAgTClNvbWUt\n"
"U3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDEUMBIGA1UE\n"
"AxMLZm9vLmJhci5jb20wgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBANFEiTNr\n"
"xDGehD636meTlC2yAmINuZn7pU9CC4BudfdDAI2YdoB9h9YqRk773EYAveAfSMYg\n"
"/ySzMlzz+yb8skZwctrocJYGpgB4N0BpmkGt7VSK9qwT4mRXqL6G2Cvvifi4BBYP\n"
"Q4c5JvYP43cDd7/Yb7Hg3Do8tG16Zo6AXaFpAgMBAAEwDQYJKoZIhvcNAQEFBQAD\n"
"gYEAbUXoPS+AhHuO7T7KRdgwJDLyr15dwUplGwtZT+MoOnnDMRWv/0VG4QUbBwvP\n"
"5Jrrk/lRHKajXLmzrqaoiadGzj6vCOh+zuf/KAOhQjvYtZyL0b727W1Sf2i7Cij+\n"
"ublOOHR0hldn/XqR7hKfZ/uIPnznQeKkVGjrEs223vtf7cI=\n"
"-----END CERTIFICATE-----\n";

char *test_server_ca_cert = "-----BEGIN CERTIFICATE-----\n"
"MIICLzCCAZgCCQDTeVgTQPW40zANBgkqhkiG9w0BAQUFADBbMQswCQYDVQQGEwJE\n"
"RTETMBEGA1UECBMKU29tZS1TdGF0ZTEhMB8GA1UEChMYSW50ZXJuZXQgV2lkZ2l0\n"
"cyBQdHkgTHRkMRQwEgYDVQQDEwtmb28uYmFyLmNvbTAgFw0xNTAyMjYxMjAwMzla\n"
"GA8yMTE1MDIwMjEyMDAzOVowWzELMAkGA1UEBhMCREUxEzARBgNVBAgTClNvbWUt\n"
"U3RhdGUxITAfBgNVBAoTGEludGVybmV0IFdpZGdpdHMgUHR5IEx0ZDEUMBIGA1UE\n"
"AxMLZm9vLmJhci5jb20wgZ8wDQYJKoZIhvcNAQEBBQADgY0AMIGJAoGBANFEiTNr\n"
"xDGehD636meTlC2yAmINuZn7pU9CC4BudfdDAI2YdoB9h9YqRk773EYAveAfSMYg\n"
"/ySzMlzz+yb8skZwctrocJYGpgB4N0BpmkGt7VSK9qwT4mRXqL6G2Cvvifi4BBYP\n"
"Q4c5JvYP43cDd7/Yb7Hg3Do8tG16Zo6AXaFpAgMBAAEwDQYJKoZIhvcNAQEFBQAD\n"
"gYEAbUXoPS+AhHuO7T7KRdgwJDLyr15dwUplGwtZT+MoOnnDMRWv/0VG4QUbBwvP\n"
"5Jrrk/lRHKajXLmzrqaoiadGzj6vCOh+zuf/KAOhQjvYtZyL0b727W1Sf2i7Cij+\n"
"ublOOHR0hldn/XqR7hKfZ/uIPnznQeKkVGjrEs223vtf7cI=\n"
"-----END CERTIFICATE-----\n";

char *test_server_key = "-----BEGIN RSA PRIVATE KEY-----\n"
"MIICXgIBAAKBgQDRRIkza8QxnoQ+t+pnk5QtsgJiDbmZ+6VPQguAbnX3QwCNmHaA\n"
"fYfWKkZO+9xGAL3gH0jGIP8kszJc8/sm/LJGcHLa6HCWBqYAeDdAaZpBre1Uivas\n"
"E+JkV6i+htgr74n4uAQWD0OHOSb2D+N3A3e/2G+x4Nw6PLRtemaOgF2haQIDAQAB\n"
"AoGBAM9w9eRwLmLVdNhLLeSQqXGGpNAYNOTMTDk+CfK9DNkXpQO3n7iNN0r4Swve\n"
"pKML9yylNlmYufLiY8k63brvAbP/Tfg2cbzg47fv+kacqYgaH6aoII++UEAoF+pM\n"
"HgdINRIRn+wknsNxdxE+YEJW/+XfhHiwD31RKBFOYw0NL3PRAkEA+pTwf1CfXBLP\n"
"Ujap10y883PQpX+lLyFzMT2BEu5C1WfUSjHiyzZyb6utYZ9U1PlTvaUXSJ3guNcl\n"
"VVvwjll/rQJBANXK6H9xy959Y7EKfxT41BDHQoXfmEIcLSz1wgWSeKwgbFF4+n3g\n"
"JoHG1n4hQ7D6OV41oh18XXYFBE9Ienyw8C0CQEHs0WENev+kSzsb+o8UN1ntjGUe\n"
"Mf02VbIMtlqeqKKwkF98xGgmSPEsP49BdfYaKnfoaTnHn4nBwKa2a5Fn5nkCQQCr\n"
"nApwcmnRGBlzvRcxQGMJbMjrKQXQ20kv871gN6iBki0gYNnBPLHsLi1yZUUuxExU\n"
"YPzWakOjPnetJGKdwHGpAkEAnMDbIjYpg9WYtx4l5q8R8u1USf8ndybDQehite7W\n"
"nzpG25y4ERn1b0M8TJ0xK0y2b8pMWBYlavUYkCYCfWOAsw==\n"
"-----END RSA PRIVATE KEY-----";


void generic_setup()
{
	ck_assert(scep_init(&handle) == SCEPE_OK);
	scep_log = BIO_new_fp(stdout, BIO_NOCLOSE);
	scep_conf_set(handle, SCEPCFG_LOG, scep_log);
	scep_conf_set(handle, SCEPCFG_VERBOSITY, DEBUG);
}

void generic_teardown()
{
	BIO_flush(scep_log);
	BIO_free(scep_log);
	scep_cleanup(handle);
}

void make_message_data(
		X509 **sig_cert, EVP_PKEY **sig_key, X509_REQ **req, 
		X509 **enc_cert, const EVP_CIPHER **enc_alg)
{
	BIO *b;
	if(*sig_cert == NULL || *sig_key == NULL) {
		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_key);
		PEM_read_bio_PrivateKey(b, sig_key, 0, 0);
		BIO_free(b);

		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_crt);
		PEM_read_bio_X509(b, &dec_cert, 0, 0);
		BIO_free(b);
		
		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_key);
		PEM_read_bio_PrivateKey(b, &dec_key, 0, 0);
		BIO_free(b);

		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_crt);
		PEM_read_bio_X509(b, sig_cert, 0, 0);
		BIO_free(b);
	}

	if(*req == NULL) {
		*req = X509_REQ_new();
		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_new_csr);
		PEM_read_bio_X509_REQ(b, req, 0, 0);
		BIO_free(b);
	}

	if(*enc_cert == NULL) {
		b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_server_cert);
		PEM_read_bio_X509(b, enc_cert, 0, 0);
		BIO_free(b);
	}

	if(*enc_alg == NULL)
		*enc_alg = EVP_des_ede3_cbc();
}

PKCS7 *make_pkcsreq_message(
		X509 *sig_cert, EVP_PKEY *sig_key, X509_REQ *req, 
		X509 *enc_cert, const EVP_CIPHER *enc_alg)
{
	PKCS7 *p7;
	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);
	ck_assert(scep_pkcsreq(
		handle, req, sig_cert, sig_key, enc_cert, enc_alg, &p7) == SCEPE_OK);
	return p7;
}

SCEP_DATA *make_unwrap_message(
		X509 *enc_cert, EVP_PKEY *enc_key)
{
	X509 *sig_cert = NULL;
	EVP_PKEY *sig_key = NULL;
	X509_REQ *req = NULL;
	const EVP_CIPHER *enc_alg = NULL;
	PKCS7 *p7;
	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);
	scep_pkcsreq(handle, req, sig_cert, sig_key, enc_cert, enc_alg, &p7);
	SCEP_DATA *pkiMessage = NULL;
	if(enc_key == NULL) {
		BIO *b = BIO_new(BIO_s_mem());
		BIO_puts(b, test_server_key);
		PEM_read_bio_PrivateKey(b, &enc_key, 0, 0);
		BIO_free(b);
	}
	ck_assert(scep_unwrap(
		handle, p7, enc_cert, enc_key, &pkiMessage) == SCEPE_OK);
	return pkiMessage;
}


PKCS7 *make_gci_message(
		X509 *sig_cert, EVP_PKEY *sig_key, X509_REQ *req,
		X509 *enc_cert, const EVP_CIPHER *enc_alg, X509 *cacert)
{
	PKCS7 *p7;
	BIO *b;

	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);

	b = BIO_new(BIO_s_mem());
	BIO_puts(b, test_server_ca_cert);
	PEM_read_bio_X509(b, &cacert, 0, 0);
	BIO_free(b);

	ck_assert(scep_get_cert_initial(
		handle, req, sig_cert, sig_key,
		cacert, enc_cert, enc_alg, &p7) == SCEPE_OK);
	return p7;
}


PKCS7 *make_gc_message(
		X509 *sig_cert, EVP_PKEY *sig_key, X509_REQ *req,
		X509 *enc_cert, const EVP_CIPHER *enc_alg, X509 *cacert)
{
	PKCS7 *p7;

	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);

	ck_assert(scep_get_cert(
		handle, req, sig_cert, sig_key,
		sig_cert, enc_cert, enc_alg, &p7) == SCEPE_OK);
	return p7;
}


PKCS7 *make_gcrl_message(
		X509 *sig_cert, EVP_PKEY *sig_key, X509_REQ *req,
		X509 *enc_cert, const EVP_CIPHER *enc_alg, X509 *cacert)
{
	PKCS7 *p7;

	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);

	ck_assert(scep_get_crl(
		handle, req, sig_cert, sig_key,
		sig_cert, enc_cert, enc_alg, &p7) == SCEPE_OK);
	return p7;
}

SCEP_ERROR PKCS7_get_content(PKCS7 *p7, PKCS7 **result) {
	BIO *pkcs7bio = NULL;
	PKCS7 *content = NULL;
	SCEP_ERROR error = SCEPE_OK;

	pkcs7bio = PKCS7_dataInit(p7, NULL);
	if(!pkcs7bio)
		OSSL_ERR("Could not create BIO for reading PKCS7 content");

	content = d2i_PKCS7_bio(pkcs7bio, NULL);
	if(!content)
		OSSL_ERR("Could not read from content BIO");

	*result = content;
finally:	
	if(error != SCEPE_OK) {
		if(pkcs7bio)
			BIO_free(pkcs7bio);
		if(content)
			PKCS7_free(content);
	}
	return error;
}

BIO *get_decrypted_data(PKCS7 *p7)
{
	// currently failing because we cannot extract data from PKCS#7 data type
	PKCS7 *p7enc = NULL, *p7_inner;
	BIO *outbio;
	ck_assert(PKCS7_get_content(p7, &p7enc) == SCEPE_OK);
	outbio = BIO_new(BIO_s_mem());

	// version == 0
	ck_assert_str_eq("0", i2s_ASN1_INTEGER(NULL, p7enc->d.enveloped->version));

	// decrypt and check content
	PKCS7_decrypt(p7enc, dec_key, dec_cert, outbio, 0);
	return outbio;
}

void unwrap_setup()
{
	generic_setup();
	pkiMessage = make_unwrap_message(NULL, NULL);
}

void unwrap_teardown()
{
	generic_teardown();
}

void pkcsreq_setup()
{
	generic_setup();
	p7 = make_pkcsreq_message(NULL, NULL, NULL, NULL, NULL);
	scep_conf_set(handle, SCEPCFG_FLAG_SET, SCEP_SKIP_SIGNER_CERT);
	p7_nosigcert = make_pkcsreq_message(NULL, NULL, NULL, NULL, NULL);
}

void pkcsreq_teardown()
{
	PKCS7_free(p7);
	PKCS7_free(p7_nosigcert);
	generic_teardown();
}

void gci_setup()
{
	generic_setup();
	p7 = make_gci_message(NULL, NULL, NULL, NULL, NULL, NULL);
	scep_conf_set(handle, SCEPCFG_FLAG_SET, SCEP_SKIP_SIGNER_CERT);
	p7_nosigcert = make_gci_message(NULL, NULL, NULL, NULL, NULL, NULL);
}

void gci_teardown()
{
	PKCS7_free(p7);
	PKCS7_free(p7_nosigcert);
	generic_teardown();
}

void gc_setup()
{
	generic_setup();
	p7 = make_gc_message(NULL, NULL, NULL, NULL, NULL, NULL);
	scep_conf_set(handle, SCEPCFG_FLAG_SET, SCEP_SKIP_SIGNER_CERT);
	p7_nosigcert = make_gc_message(NULL, NULL, NULL, NULL, NULL, NULL);
}

void gc_teardown()
{
	PKCS7_free(p7);
	PKCS7_free(p7_nosigcert);
	generic_teardown();
}

void gcrl_setup()
{
	generic_setup();
	p7 = make_gcrl_message(NULL, NULL, NULL, NULL, NULL, NULL);
	scep_conf_set(handle, SCEPCFG_FLAG_SET, SCEP_SKIP_SIGNER_CERT);
	p7_nosigcert = make_gcrl_message(NULL, NULL, NULL, NULL, NULL, NULL);
}

ASN1_STRING *get_attribute(PKCS7 *message, int nid) {
	STACK_OF(PKCS7_SIGNER_INFO) *sk_si = PKCS7_get_signer_info(message);
	ck_assert(sk_PKCS7_SIGNER_INFO_num(sk_si) == 1);
	PKCS7_SIGNER_INFO *si = sk_PKCS7_SIGNER_INFO_value(sk_si, 0);

	ASN1_TYPE *attr = PKCS7_get_signed_attribute(si, nid);
	return attr->value.printablestring;
}

char *get_attribute_data(PKCS7 *message, int nid) {
	return ASN1_STRING_data(get_attribute(message, nid));
}

START_TEST(test_unwrap_message)
{
	ck_assert_int_ne(NULL, pkiMessage);
	ck_assert_int_eq(1, pkiMessage->initialEnrollment);
	ck_assert_str_eq(
		"2F3C88114C283E9A6CD57BB8266CE313DB0BEE0DAF769D770C4E5FFB9C4C1016",
		pkiMessage->transactionID);
	ck_assert_str_eq("19", pkiMessage->messageType);
	ck_assert_int_eq(19, pkiMessage->messageType_int);
	ck_assert_int_ne(NULL, pkiMessage->request);
	ck_assert_int_ne(NULL, pkiMessage->senderNonce);
	ck_assert_str_eq("FOOBARTESTPWD", ASN1_STRING_data(pkiMessage->challenge_password->value.printablestring));
}
END_TEST


START_TEST(test_scep_message_asn1_version)
{
	ck_assert_str_eq("1", i2s_ASN1_INTEGER(NULL, p7->d.sign->version));
}
END_TEST

START_TEST(test_scep_message_transaction_id)
{
	ck_assert_str_eq(
		"2F3C88114C283E9A6CD57BB8266CE313DB0BEE0DAF769D770C4E5FFB9C4C1016",
		get_attribute_data(p7, handle->oids->transId));
}
END_TEST

START_TEST(test_scep_message_sender_nonce)
{
	ck_assert_int_eq(ASN1_STRING_length(get_attribute(p7, handle->oids->senderNonce)), 16);
}
END_TEST

START_TEST(test_scep_message_type)
{
	ck_assert(PKCS7_type_is_signed(p7));
}
END_TEST

START_TEST(test_scep_message_content_type)
{
	PKCS7 *content;
	ck_assert(PKCS7_type_is_data(p7->d.sign->contents));
}
END_TEST

START_TEST(test_scep_message_certificate)
{
	BIO *b = BIO_new(BIO_s_mem());
	X509 *ref_cert = NULL;
	BIO_puts(b, test_crt);
	PEM_read_bio_X509(b, &ref_cert, 0, 0);
	ck_assert(ref_cert != NULL);
	BIO_free(b);

	ck_assert(sk_X509_num(p7->d.sign->cert) == 1);
	X509 *cert = sk_X509_value(p7->d.sign->cert, 0);
	ck_assert(cert != NULL);
	ck_assert(X509_cmp(cert, ref_cert) == 0);

	ck_assert(sk_X509_num(p7_nosigcert->d.sign->cert) < 1); // -1 or 0
}
END_TEST

START_TEST(test_scep_pkcsreq)
{
	BIO *data = get_decrypted_data(p7);
	X509_REQ *csr = d2i_X509_REQ_bio(data, NULL);
	ck_assert(csr != NULL);

	data = BIO_new(BIO_s_mem());
	BIO_puts(data, test_new_csr);
	X509_REQ *ref_csr = PEM_read_bio_X509_REQ(data, NULL, 0, 0);
	BIO_free(data);

	ck_assert_int_eq(X509_REQ_cmp(csr, ref_csr), 0);
	ck_assert_str_eq(
		MESSAGE_TYPE_PKCSREQ,
		get_attribute_data(p7, handle->oids->messageType));
}
END_TEST

START_TEST(test_scep_pkcsreq_missing_dn)
{
	BIGNUM *bne = BN_new();
	ck_assert_int_eq(BN_set_word(bne, RSA_F4), 1);
	RSA *r = RSA_new();
	ck_assert_int_ne(RSA_generate_key_ex(r, 2048, bne, NULL), 0);

	X509_REQ *req = X509_REQ_new();
	ck_assert_int_ne(X509_REQ_set_version(req, 1), 0);

	EVP_PKEY *key = EVP_PKEY_new();
	EVP_PKEY_assign_RSA(key, r);
	ck_assert_int_ne(X509_REQ_set_pubkey(req, key), 0);
	ck_assert_int_ne(X509_REQ_sign(req, key, EVP_sha1()), 0);

	X509 *sig_cert = NULL, *enc_cert = NULL;
	EVP_PKEY *sig_key = NULL;
	const EVP_CIPHER *enc_alg = NULL;
	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);
	ck_assert(scep_pkcsreq(handle, req, sig_cert, sig_key, enc_cert, enc_alg, &p7) == SCEPE_INVALID_CONTENT);
	ck_assert(p7 == NULL);
}
END_TEST

START_TEST(test_scep_pkcsreq_missing_pubkey)
{
	BIGNUM *bne = BN_new();
	ck_assert_int_eq(BN_set_word(bne, RSA_F4), 1);
	RSA *r = RSA_new();
	ck_assert_int_ne(RSA_generate_key_ex(r, 2048, bne, NULL), 0);

	X509_REQ *req = X509_REQ_new();
	ck_assert_int_ne(X509_REQ_set_version(req, 1), 0);

	X509_NAME *name = X509_REQ_get_subject_name(req);
	ck_assert_int_ne(X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, "DE", -1, -1, 0), 0);

	EVP_PKEY *key = EVP_PKEY_new();
	EVP_PKEY_assign_RSA(key, r);
	ck_assert_int_ne(X509_REQ_sign(req, key, EVP_sha1()), 0);

	X509 *sig_cert = NULL, *enc_cert = NULL;
	EVP_PKEY *sig_key = NULL;
	const EVP_CIPHER *enc_alg = NULL;
	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);
	ck_assert(scep_pkcsreq(handle, req, sig_cert, sig_key, enc_cert, enc_alg, &p7) == SCEPE_INVALID_CONTENT);
	ck_assert(p7 == NULL);
}
END_TEST

START_TEST(test_scep_pkcsreq_missing_challenge_password)
{
	BIGNUM *bne = BN_new();
	ck_assert_int_eq(BN_set_word(bne, RSA_F4), 1);
	RSA *r = RSA_new();
	ck_assert_int_ne(RSA_generate_key_ex(r, 2048, bne, NULL), 0);

	X509_REQ *req = X509_REQ_new();
	ck_assert_int_ne(X509_REQ_set_version(req, 1), 0);

	X509_NAME *name = X509_REQ_get_subject_name(req);
	ck_assert_int_ne(X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, "DE", -1, -1, 0), 0);

	EVP_PKEY *key = EVP_PKEY_new();
	EVP_PKEY_assign_RSA(key, r);
	ck_assert_int_ne(X509_REQ_set_pubkey(req, key), 0);
	ck_assert_int_ne(X509_REQ_sign(req, key, EVP_sha1()), 0);

	X509 *sig_cert = NULL, *enc_cert = NULL;
	EVP_PKEY *sig_key = NULL;
	const EVP_CIPHER *enc_alg = NULL;
	make_message_data(&sig_cert, &sig_key, &req, &enc_cert, &enc_alg);
	ck_assert(scep_pkcsreq(handle, req, sig_cert, sig_key, enc_cert, enc_alg, &p7) == SCEPE_INVALID_CONTENT);
	ck_assert(p7 == NULL);
}
END_TEST


START_TEST(test_scep_gci)
{
	BIO *data = get_decrypted_data(p7);

	const unsigned char *data_buf;
	int data_buf_len = BIO_get_mem_data(data, &data_buf);
	ck_assert_int_ne(data_buf_len, 0);

	ck_assert_str_eq(
		MESSAGE_TYPE_GETCERTINITIAL,
		get_attribute_data(p7, handle->oids->messageType));

	PKCS7_ISSUER_AND_SUBJECT *ias = NULL;
	d2i_PKCS7_ISSUER_AND_SUBJECT(&ias, &data_buf, data_buf_len);
	ck_assert(ias != NULL);
	ck_assert_str_eq(X509_NAME_oneline(ias->subject, NULL, 0), "/C=AU/ST=Some-State/O=Internet Widgits Pty Ltd/CN=foo.bar");
	ck_assert_str_eq(X509_NAME_oneline(ias->issuer, NULL, 0), "/C=DE/ST=Some-State/O=Internet Widgits Pty Ltd/CN=foo.bar.com");
}
END_TEST


START_TEST(test_scep_gc)
{
	BIO *data = get_decrypted_data(p7);

	const unsigned char *data_buf;
	int data_buf_len = BIO_get_mem_data(data, &data_buf);
	ck_assert_int_ne(data_buf_len, 0);

	ck_assert_str_eq(
		MESSAGE_TYPE_GETCERT,
		get_attribute_data(p7, handle->oids->messageType));

	PKCS7_ISSUER_AND_SERIAL *ias = NULL;
	d2i_PKCS7_ISSUER_AND_SERIAL(&ias, &data_buf, data_buf_len);
	ck_assert(ias != NULL);
	ck_assert_str_eq(X509_NAME_oneline(ias->issuer, NULL, 0), "/C=DE/ST=Some-State/O=Internet Widgits Pty Ltd/CN=foo.bar.com");
	ck_assert_str_eq("15238307653902252243", i2s_ASN1_INTEGER(NULL, ias->serial));

}
END_TEST


START_TEST(test_scep_gcrl)
{
	BIO *data = get_decrypted_data(p7);

	const unsigned char *data_buf;
	int data_buf_len = BIO_get_mem_data(data, &data_buf);
	ck_assert_int_ne(data_buf_len, 0);

	ck_assert_str_eq(
		MESSAGE_TYPE_GETCRL,
		get_attribute_data(p7, handle->oids->messageType));

	PKCS7_ISSUER_AND_SERIAL *ias = NULL;
	d2i_PKCS7_ISSUER_AND_SERIAL(&ias, &data_buf, data_buf_len);
	ck_assert(ias != NULL);
	ck_assert_str_eq(X509_NAME_oneline(ias->issuer, NULL, 0), "/C=DE/ST=Some-State/O=Internet Widgits Pty Ltd/CN=foo.bar.com");
	ck_assert_str_eq("15238307653902252243", i2s_ASN1_INTEGER(NULL, ias->serial));

}
END_TEST


Suite * scep_message_suite(void)
{
	Suite *s = suite_create("Message");

	/*test unwrapping*/
	TCase *tc_unwrap_msg = tcase_create("Unwrap Message");
	tcase_add_checked_fixture(tc_unwrap_msg, unwrap_setup, unwrap_teardown);
	tcase_add_test(tc_unwrap_msg, test_unwrap_message);
	suite_add_tcase(s, tc_unwrap_msg);

	/* PKCSReq tests */
	TCase *tc_pkcsreq_msg = tcase_create("PKCSReq Message");
	tcase_add_checked_fixture(tc_pkcsreq_msg, pkcsreq_setup, pkcsreq_teardown);

	tcase_add_test(tc_pkcsreq_msg, test_scep_message_asn1_version);
	tcase_add_test(tc_pkcsreq_msg, test_scep_message_transaction_id);
	tcase_add_test(tc_pkcsreq_msg, test_scep_message_sender_nonce);
	tcase_add_test(tc_pkcsreq_msg, test_scep_message_type);
	tcase_add_test(tc_pkcsreq_msg, test_scep_message_content_type);
	tcase_add_test(tc_pkcsreq_msg, test_scep_message_certificate);
	tcase_add_test(tc_pkcsreq_msg, test_scep_pkcsreq);
	suite_add_tcase(s, tc_pkcsreq_msg);

	TCase *tc_pkcsreq_errors = tcase_create("PKCSReq Invalid");
	tcase_add_checked_fixture(tc_pkcsreq_errors, generic_setup, generic_teardown);
	tcase_add_test(tc_pkcsreq_errors, test_scep_pkcsreq_missing_dn);
	tcase_add_test(tc_pkcsreq_errors, test_scep_pkcsreq_missing_pubkey);
	tcase_add_test(tc_pkcsreq_errors, test_scep_pkcsreq_missing_challenge_password);
	suite_add_tcase(s, tc_pkcsreq_errors);

	/* GetCertInitial tests */
	TCase *tc_gci_msg = tcase_create("GetCertInitial Message");
	tcase_add_checked_fixture(tc_gci_msg, gci_setup, gci_teardown);
	tcase_add_test(tc_gci_msg, test_scep_message_transaction_id);
	tcase_add_test(tc_gci_msg, test_scep_message_sender_nonce);
	tcase_add_test(tc_gci_msg, test_scep_message_certificate);
	tcase_add_test(tc_gci_msg, test_scep_gci);
	suite_add_tcase(s, tc_gci_msg);

	/* GetCert tests */
	TCase *tc_gc_msg = tcase_create("GetCert Message");
	tcase_add_checked_fixture(tc_gc_msg, gc_setup, gc_teardown);
	tcase_add_test(tc_gc_msg, test_scep_message_transaction_id);
	tcase_add_test(tc_gc_msg, test_scep_message_sender_nonce);
	tcase_add_test(tc_gc_msg, test_scep_message_certificate);
	tcase_add_test(tc_gc_msg, test_scep_gc);
	suite_add_tcase(s, tc_gc_msg);

	/* GetCRL tests */
	TCase *tc_gcrl_msg = tcase_create("GetRL Message");
	tcase_add_checked_fixture(tc_gcrl_msg, gcrl_setup, gc_teardown);
	tcase_add_test(tc_gcrl_msg, test_scep_message_transaction_id);
	tcase_add_test(tc_gcrl_msg, test_scep_message_sender_nonce);
	tcase_add_test(tc_gcrl_msg, test_scep_message_certificate);
	tcase_add_test(tc_gcrl_msg, test_scep_gcrl);
	suite_add_tcase(s, tc_gcrl_msg);

	return s;
}

int main(void)
{
	int number_failed;
	Suite *s = scep_message_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
