#include "scep.h"
#include<unistd.h>
SCEP_ERROR scep_p7_client_init(SCEP *handle, X509 *sig_cert, EVP_PKEY *sig_key, struct p7_data_t *p7data)
{
	SCEP_ERROR error = SCEPE_OK;

	p7data->p7 = PKCS7_new();
	if(p7data->p7 == NULL)
		OSSL_ERR("Could not create PKCS#7 data structure");

	if(!PKCS7_set_type(p7data->p7, NID_pkcs7_signed))
		OSSL_ERR("Could not set PKCS#7 type");

	p7data->signer_info = PKCS7_add_signature(
		p7data->p7, sig_cert, sig_key, handle->configuration->sigalg);
	if(p7data->signer_info == NULL)
		OSSL_ERR("Could not create new PKCS#7 signature");

	/* Certificate to verify signature
	 * See the discussion at https://github.com/Javex/libscep/issues/3
	 * on this issue about when, how and why we need this. It is not required
	 * by either PKCS#7 or SCEP
	 */
	if(!(handle->configuration->flags & SCEP_SKIP_SIGNER_CERT))
		if(!PKCS7_add_certificate(p7data->p7, sig_cert))
			OSSL_ERR("Could not add signer certificate");

	/* sender nonce */
	if(RAND_bytes(p7data->sender_nonce, NONCE_LENGTH) == 0)
		OSSL_ERR("Could not generate random sender nonce");

	/* Initialize content */
	if(!PKCS7_content_new(p7data->p7, NID_pkcs7_data))
		OSSL_ERR("Could not create inner PKCS#7 data structure");
	p7data->bio = PKCS7_dataInit(p7data->p7, NULL);
	if(!p7data->bio)
		OSSL_ERR("Could not initialize PKCS#7 data");

finally:
	if(error != SCEPE_OK) {
		if(p7data->p7)
			PKCS7_free(p7data->p7);
		if(p7data->bio)
			BIO_free(p7data->bio);
		if(p7data->transaction_id)
			free(p7data->transaction_id);
	}
	return error;
}

SCEP_ERROR scep_p7_final(SCEP *handle, struct p7_data_t *p7data, PKCS7 **p7)
{
	SCEP_ERROR error = SCEPE_OK;

	if(!PKCS7_dataFinal(p7data->p7, p7data->bio))
		OSSL_ERR("Could not finalize PKCS#7 data");

	*p7 = p7data->p7;
finally:
	return error;
}

SCEP_ERROR scep_pkcsreq(
	SCEP *handle, X509_REQ *req, X509 *sig_cert, EVP_PKEY *sig_key,
		X509 *enc_cert, PKCS7 **pkiMessage)
{
	BIO *databio = NULL;
	EVP_PKEY *req_pubkey = NULL;
	SCEP_ERROR error = SCEPE_OK;
	struct p7_data_t p7data;
	X509_NAME *subject;
	char *subject_str = NULL;
	int passwd_index;

	subject = X509_REQ_get_subject_name(req);
	subject_str = X509_NAME_oneline(subject, NULL, 0);
	if(!strlen(subject_str)) {
		scep_log(handle, ERROR, "Need a subject on CSR as required by SCEP protocol specification");
		return SCEPE_INVALID_CONTENT;
	}
	scep_log(handle, INFO, "Certificate subject: %s", subject_str);
	free(subject_str);

	req_pubkey = X509_REQ_get_pubkey(req);
	if(!req_pubkey) {
		scep_log(handle, ERROR, "Need public key on CSR");
		return SCEPE_INVALID_CONTENT;
	}
	passwd_index = X509_REQ_get_attr_by_NID(req, NID_pkcs9_challengePassword, -1);
	if(passwd_index == -1) {
		scep_log(handle, ERROR, "Need challenge password field on CSR");
		return SCEPE_INVALID_CONTENT;
	}

	databio = BIO_new(BIO_s_mem());
	if(!databio)
		OSSL_ERR("Could not create data BIO");

	if(i2d_X509_REQ_bio(databio, req) <= 0)
		OSSL_ERR("Could not read request into data BIO");

	if((error = scep_p7_client_init(handle, sig_cert, sig_key, &p7data)) != SCEPE_OK)
		goto finally;

	/* transaction ID */
	if((error = scep_calculate_transaction_id_pubkey(handle, req_pubkey, &p7data.transaction_id)) != SCEPE_OK) {
		scep_log(handle, FATAL, "Could create transaction ID");
		goto finally;
	}

	if((error = scep_pkiMessage(
			handle, SCEP_MSG_PKCSREQ_STR,
			databio, enc_cert, &p7data)) != SCEPE_OK)
		goto finally;
	if((error = scep_p7_final(handle, &p7data, pkiMessage)) != SCEPE_OK)
		goto finally;

finally:
	if(databio)
		BIO_free(databio);
	if(req_pubkey)  // needed?
		EVP_PKEY_free(req_pubkey);
	return error;
}

SCEP_ERROR scep_certrep(
	SCEP *handle,
	char *transactionID,
	unsigned char *senderNonce,
	char * pkiStatus, /*required*/
	char *failInfo, /*required, if pkiStatus = failure*/
	X509 *requestedCert, /*iff success, issuedCert (PKCSReq, GetCertInitial, or other one if GetCert*/
	X509 *sig_cert, EVP_PKEY *sig_key, /*required*/
	X509 *enc_cert, /*required iff success, alternative:read out from request, alternative 2: put into SCEP_DATA when unwrapping*/
	STACK_OF(X509) *additionalCerts, /*optional (in success case): additional certs to be included*/
	X509_CRL *crl, /*mutually exclusive to requestedCert*/
	PKCS7 **pkiMessage) /*return pkcs7*/ 
	/*Note: additionalCerts does not include requestedCert in order to ensure that requestedCert is first in list*/
{	ASN1_PRINTABLESTRING *asn1_recipient_nonce, *asn1_pkiStatus, *asn1_failInfo;
	SCEP_ERROR error = SCEPE_OK;
	char *failInfo_nr;

	if(sig_cert == NULL)
		OSSL_ERR("signer Cert is required");

	if(sig_key == NULL)
		OSSL_ERR("signer Key is required");

	if(pkiStatus == NULL)
		OSSL_ERR("pkiStatus is required");

	/*TODO: add string attributes to header*/
	if(strcmp(pkiStatus, "FAILURE") == 0)
		if(failInfo == NULL)
			OSSL_ERR("FAILURE requires a failInfo");

	if(strcmp(pkiStatus, "SUCCESS") == 0) {
		if(enc_cert == NULL)
			OSSL_ERR("SUCCESS requires an encryption cert");
		if(!(requestedCert == NULL) ^ (crl == NULL))
			OSSL_ERR("requested cert and crl are mutually exclusive");
		if((additionalCerts != NULL) && (requestedCert == NULL))
			OSSL_ERR("additional certs can only be added if a requested cert is included");
	}

	/*TODO: way more checks e.g. whether SCEP_DATA contains transID etc*/
	
	//PKCS7 *local_pkiMessage;
	struct p7_data_t *p7data = malloc(sizeof(*p7data));
	if(!p7data) {
		error = SCEPE_MEMORY;
		goto finally;
	}
	memset(p7data, 0, sizeof(*p7data));

	/*generic for all certrep types*/
	p7data->p7 = PKCS7_new();
	if(p7data->p7 == NULL)
		OSSL_ERR("Could not create PKCS#7 data structure");

	if(!PKCS7_set_type(p7data->p7, NID_pkcs7_signed))
		OSSL_ERR("Could not set PKCS#7 type");

	p7data->signer_info = PKCS7_add_signature(
		p7data->p7, sig_cert, sig_key, handle->configuration->sigalg);
	if(p7data->signer_info == NULL)
		OSSL_ERR("Could not create new PKCS#7 signature");

	if(!(handle->configuration->flags & SCEP_SKIP_SIGNER_CERT))
		if(!PKCS7_add_certificate(p7data->p7, sig_cert))
			OSSL_ERR("Could not add signer certificate");

	/*TODO: Investigate, wether this is really needed*/
	if(!PKCS7_content_new(p7data->p7, NID_pkcs7_data))
		OSSL_ERR("Could not create inner PKCS#7 data structure");


	p7data->transaction_id = strdup(transactionID);
	if(!p7data->transaction_id) {
		error = SCEPE_MEMORY;
		goto finally;
	}

	memcpy(p7data->sender_nonce, senderNonce, NONCE_LENGTH);
	if(!p7data->sender_nonce) {
		error = SCEPE_MEMORY;
		goto finally;
	}

	p7data->bio = PKCS7_dataInit(p7data->p7, NULL);
	if(!p7data->bio)
		OSSL_ERR("Could not initialize PKCS#7 data");

	if(strcmp(pkiStatus,"PENDING") == 0) {
		/*encryption content MUST be ommited*/
		if((error = scep_pkiMessage(
				handle, SCEP_MSG_CERTREP_STR,
				NULL, NULL, p7data)) != SCEPE_OK)
			goto finally;

		/* pkiStatus */
		asn1_pkiStatus = ASN1_PRINTABLESTRING_new();
		if(asn1_pkiStatus == NULL)
			OSSL_ERR("Could not create ASN1 pkiStatus object");
		if(!ASN1_STRING_set(asn1_pkiStatus, SCEP_PKISTATUS_PENDING, -1))
			OSSL_ERR("Could not set ASN1 pkiStatus object");
		if(!PKCS7_add_signed_attribute(
				p7data->signer_info, handle->oids->pkiStatus, V_ASN1_PRINTABLESTRING,
				asn1_pkiStatus))
			OSSL_ERR("Could not add attribute for pkiStatus");
	}
	else if(strcmp(pkiStatus,"FAILURE") == 0) {
		/*encryption content MUST be ommited*/
		if((error = scep_pkiMessage(
				handle, SCEP_MSG_CERTREP_STR,
				NULL, NULL, p7data)) != SCEPE_OK)
			goto finally;

		/* pkiStatus */
		asn1_pkiStatus = ASN1_PRINTABLESTRING_new();
		if(asn1_pkiStatus == NULL)
			OSSL_ERR("Could not create ASN1 pkiStatus object");
		if(!ASN1_STRING_set(asn1_pkiStatus, SCEP_PKISTATUS_FAILURE, -1))
			OSSL_ERR("Could not set ASN1 pkiStatus object");
		if(!PKCS7_add_signed_attribute(
				p7data->signer_info, handle->oids->pkiStatus, V_ASN1_PRINTABLESTRING,
				asn1_pkiStatus))
			OSSL_ERR("Could not add attribute for pkiStatus");

		
		if(strcmp(failInfo, "badAlg") == 0) {
			failInfo_nr = SCEP_FAILINFO_BADALG;
		}
		else if(strcmp(failInfo, "badMessageCheck") == 0) {
			failInfo_nr = SCEP_FAILINFO_BADMESSAGECHECK;
		}
		else if(strcmp(failInfo, "badRequest") == 0) {
			failInfo_nr = SCEP_FAILINFO_BADREQUEST;
		}
		else if(strcmp(failInfo, "badTime") == 0) {
			failInfo_nr = SCEP_FAILINFO_BADTIME;
		}
		else if(strcmp(failInfo, "badCertId") == 0) {
			failInfo_nr = SCEP_FAILINFO_BADCERTID;
		}
		else {
			OSSL_ERR("Unsupported failInfo");
		}
				
		asn1_failInfo = ASN1_PRINTABLESTRING_new();
		if(asn1_failInfo == NULL)
			OSSL_ERR("Could not create ASN1 failInfo object");
		if(!ASN1_STRING_set(asn1_failInfo, failInfo_nr, -1))
			OSSL_ERR("Could not set ASN1 failInfo object");
		if(!PKCS7_add_signed_attribute(
				p7data->signer_info, handle->oids->failInfo, V_ASN1_PRINTABLESTRING,
				asn1_failInfo))
			OSSL_ERR("Could not add attribute for failInfo");
	}
	else if(strcmp(pkiStatus,"SUCCESS") == 0) {
		/*create degen p7*/
		PKCS7 *degenP7 = NULL;
		if(!(make_degenP7(
 				handle, requestedCert, additionalCerts, crl, &degenP7) == SCEPE_OK))
			OSSL_ERR("Could not create degenP7");

		/*make it to BIO for encryption*/
		BIO *databio = BIO_new(BIO_s_mem());
		if(!databio)
			OSSL_ERR("Could not create data BIO");
		if(i2d_PKCS7_bio(databio, degenP7) <= 0)
			OSSL_ERR("Could not read degenP7 into data BIO");

		if((error = scep_pkiMessage(
				handle, SCEP_MSG_CERTREP_STR,
				databio, enc_cert, p7data)) != SCEPE_OK)
			goto finally;

		/* pkiStatus */
		asn1_pkiStatus = ASN1_PRINTABLESTRING_new();
		if(asn1_pkiStatus == NULL)
			OSSL_ERR("Could not create ASN1 pkiStatus object");
		if(!ASN1_STRING_set(asn1_pkiStatus, SCEP_PKISTATUS_SUCCESS, -1))
			OSSL_ERR("Could not set ASN1 pkiStatus object");
		if(!PKCS7_add_signed_attribute(
				p7data->signer_info, handle->oids->pkiStatus, V_ASN1_PRINTABLESTRING,
				asn1_pkiStatus))
			OSSL_ERR("Could not add attribute for pkiStatus");

	}
	else {
		OSSL_ERR("unknown pkiStatus");
	}


	/* set recipient nonce to sender nonce*/
	/*TODO: User should be able to chose different senderNonce*/
	asn1_recipient_nonce = ASN1_OCTET_STRING_new();
	if(asn1_recipient_nonce == NULL)
		OSSL_ERR("Could not create ASN1 recipient nonce object");
	if(!ASN1_OCTET_STRING_set(asn1_recipient_nonce, p7data->sender_nonce, NONCE_LENGTH))
		OSSL_ERR("Could not set ASN1 recipient nonce object");
	if(!PKCS7_add_signed_attribute(
			p7data->signer_info, handle->oids->recipientNonce, V_ASN1_OCTET_STRING,
			asn1_recipient_nonce))
		OSSL_ERR("Could not add attribute for recipient nonce");


	/*searching in openssl source is like a box of chocklate...*/
	/*yes, set content and than detach it again. That way, OID is present but not its content. Must be to be conform with PKCS7 spec*/
	//PKCS7_set_detached(p7data->p7, 1);
	if((error = scep_p7_final(handle, p7data, pkiMessage)) != SCEPE_OK)
		goto finally;

finally:
	return error;
}

SCEP_ERROR scep_get_cert_initial(
		SCEP *handle, X509_REQ *req, X509 *sig_cert, EVP_PKEY *sig_key,
		X509 *cacert, X509 *enc_cert,
		PKCS7 **pkiMessage)
{

	SCEP_ERROR error = SCEPE_OK;
	struct p7_data_t p7data;
	EVP_PKEY *req_pubkey = NULL;
	PKCS7_ISSUER_AND_SUBJECT *ias;
	unsigned char *ias_data = NULL;
	int ias_data_size;
	BIO *databio;
	char *subject_str = NULL, *issuer_str = NULL;

	req_pubkey = X509_REQ_get_pubkey(req);
	if(!req_pubkey) {
		scep_log(handle, ERROR, "Need public key on CSR");
		return SCEPE_INVALID_CONTENT;
	}

	ias = PKCS7_ISSUER_AND_SUBJECT_new();
	if(!ias)
		OSSL_ERR("Could not create new issuer and subject structure");

	ias->subject = X509_REQ_get_subject_name(req);
	if(!ias->subject)
		OSSL_ERR("Could not get subject from request");
	subject_str = X509_NAME_oneline(ias->subject, NULL, 0);
	scep_log(handle, INFO, "Request subject is %s", subject_str);

	ias->issuer = X509_get_issuer_name(cacert);
	if(!ias->issuer)
		OSSL_ERR("Could not get issuer name for CA cert");
	issuer_str = X509_NAME_oneline(ias->issuer, NULL, 0);
	scep_log(handle, INFO, "Issuer Name is %s", issuer_str);

	ias_data_size = i2d_PKCS7_ISSUER_AND_SUBJECT(ias, &ias_data);
	if(!ias_data_size)
		OSSL_ERR("Could not extract issuer and subject data");

	databio = BIO_new(BIO_s_mem());
	if(!databio)
		OSSL_ERR("Could not create data BIO");

	if(!BIO_write(databio, ias_data, ias_data_size))
		OSSL_ERR("Could not write issuer and subject data into BIO");

	if((error = scep_p7_client_init(handle, sig_cert, sig_key, &p7data)))
		goto finally;

	/* transaction ID */
	if((error = scep_calculate_transaction_id_pubkey(handle, req_pubkey, &p7data.transaction_id)) != SCEPE_OK) {
		scep_log(handle, FATAL, "Could create transaction ID");
		goto finally;
	}

	if((error = scep_pkiMessage(
			handle, SCEP_MSG_GETCERTINITIAL_STR, databio, enc_cert, &p7data)) != SCEPE_OK)
		goto finally;
	if((error = scep_p7_final(handle, &p7data, pkiMessage)) != SCEPE_OK)
		goto finally;

finally:
	if(databio)
		BIO_free(databio);
	return error;
}

static SCEP_ERROR _scep_get_cert_or_crl(
		SCEP *handle, X509 *sig_cert, EVP_PKEY *sig_key,
		X509_NAME *issuer, ASN1_INTEGER *serial, X509 *enc_cert,
		char *messageType, PKCS7 **pkiMessage)
{

	SCEP_ERROR error = SCEPE_OK;
	struct p7_data_t p7data;
	PKCS7_ISSUER_AND_SERIAL *ias;
	unsigned char *ias_data = NULL;
	int ias_data_size;
	BIO *databio;
	char *issuer_str = NULL;

	ias = PKCS7_ISSUER_AND_SERIAL_new();
	if(!ias)
		OSSL_ERR("Could not create new issuer and subject structure");

	ias->serial = serial;
	ias->issuer = issuer;
	issuer_str = X509_NAME_oneline(ias->issuer, NULL, 0);
	scep_log(handle, INFO, "Issuer Name is %s", issuer_str);

	ias_data_size = i2d_PKCS7_ISSUER_AND_SERIAL(ias, &ias_data);
	if(!ias_data_size)
		OSSL_ERR("Could not extract issuer and subject data");

	databio = BIO_new(BIO_s_mem());
	if(!databio)
		OSSL_ERR("Could not create data BIO");

	if(!BIO_write(databio, ias_data, ias_data_size))
		OSSL_ERR("Could not write issuer and subject data into BIO");

	if((error = scep_p7_client_init(handle, sig_cert, sig_key, &p7data)))
		goto finally;

	/* transaction ID */
	if((error = scep_calculate_transaction_id_ias_type(handle, ias, messageType, &p7data.transaction_id)) != SCEPE_OK) {
		scep_log(handle, FATAL, "Could create transaction ID");
		goto finally;
	}

	if((error = scep_pkiMessage(
			handle, messageType, databio, enc_cert, &p7data)) != SCEPE_OK)
		goto finally;
	if((error = scep_p7_final(handle, &p7data, pkiMessage)) != SCEPE_OK)
		goto finally;

finally:
	if(databio)
		BIO_free(databio);
	return error;
}

SCEP_ERROR scep_get_cert(
		SCEP *handle, X509 *sig_cert, EVP_PKEY *sig_key,
		X509_NAME *issuer, ASN1_INTEGER *serial, X509 *enc_cert,
		PKCS7 **pkiMessage)
{
	return _scep_get_cert_or_crl(
		handle, sig_cert, sig_key,
		issuer, serial, enc_cert,
		SCEP_MSG_GETCERT_STR, pkiMessage);
}

SCEP_ERROR scep_get_crl(
		SCEP *handle, X509 *sig_cert, EVP_PKEY *sig_key,
		X509 *req_cert, X509 *enc_cert,
		PKCS7 **pkiMessage)
{
	SCEP_ERROR error = SCEPE_OK;
	ASN1_INTEGER *serial = X509_get_serialNumber(req_cert);
	if(!serial)
		OSSL_ERR("Could not get serial from CA cert");

	X509_NAME *issuer = X509_get_issuer_name(req_cert);
	if(!issuer)
		OSSL_ERR("Could not get issuer name for CA cert");

	return _scep_get_cert_or_crl(
		handle, sig_cert, sig_key,
		issuer, serial, enc_cert,
		SCEP_MSG_GETCRL_STR, pkiMessage);
finally:
	return error;
}

SCEP_ERROR scep_pkiMessage(
		SCEP *handle,
		char *messageType, BIO *data,
		X509 *enc_cert,
		struct p7_data_t *p7data) {
	PKCS7 *encdata = NULL;
	SCEP_ERROR error = SCEPE_OK;
	STACK_OF(X509) *enc_certs;
	ASN1_PRINTABLESTRING *asn1_transaction_id, *asn1_message_type, *asn1_sender_nonce;
	/* transaction ID */
	asn1_transaction_id = ASN1_PRINTABLESTRING_new();
	
	if(asn1_transaction_id == NULL)
		OSSL_ERR("Could not create ASN1 TID object");
	if(!ASN1_STRING_set(asn1_transaction_id, p7data->transaction_id, -1))
		OSSL_ERR("Could not set ASN1 TID object");
	if(!PKCS7_add_signed_attribute(
			p7data->signer_info, handle->oids->transId, V_ASN1_PRINTABLESTRING,
			asn1_transaction_id))
		OSSL_ERR("Could not add attribute for transaction ID");

	/* message type */
	asn1_message_type = ASN1_PRINTABLESTRING_new();
	if(asn1_message_type == NULL)
		OSSL_ERR("Could not create ASN1 message type object");
	if(!ASN1_STRING_set(asn1_message_type, messageType, -1))
		OSSL_ERR("Could not set ASN1 message type object");
	if(!PKCS7_add_signed_attribute(
			p7data->signer_info, handle->oids->messageType, V_ASN1_PRINTABLESTRING,
			asn1_message_type))
		OSSL_ERR("Could not add attribute for message type");

	/* sender nonce */
	asn1_sender_nonce = ASN1_OCTET_STRING_new();
	if(asn1_sender_nonce == NULL)
		OSSL_ERR("Could not create ASN1 sender nonce object");
	if(!ASN1_OCTET_STRING_set(asn1_sender_nonce, p7data->sender_nonce, NONCE_LENGTH))
		OSSL_ERR("Could not set ASN1 sender nonce object");
	if(!PKCS7_add_signed_attribute(
			p7data->signer_info, handle->oids->senderNonce, V_ASN1_OCTET_STRING,
			asn1_sender_nonce))
		OSSL_ERR("Could not add attribute for sender nonce");

	/* encrypt data */
	// since we currently are a client, we always have data
	// however, if we want to support server in the future as well
	// we sould make data optional.
	// certificate to encrypt data with
	/*if data is set to NULL, it is assumed that no content should be encrypted*/
	if(!(data == NULL)) {
		enc_certs = sk_X509_new_null();
		if(!enc_certs)
			OSSL_ERR("Could not create enc cert stack");
		if(!sk_X509_push(enc_certs, enc_cert))
			OSSL_ERR("Could not push enc cert onto stack");
		encdata = PKCS7_encrypt(enc_certs, data, handle->configuration->encalg, PKCS7_BINARY);
		if(!encdata)
			OSSL_ERR("Could not encrypt data");
		// put encrypted data into p7
		if(!i2d_PKCS7_bio(p7data->bio, encdata))
			OSSL_ERR("Could not write encdata to PKCS#7 BIO");
	}
finally:
	return error;
}

SCEP_ERROR scep_unwrap_response(
		SCEP *handle, PKCS7 *pkiMessage, X509 *sig_cacert,
		X509 *request_cert, EVP_PKEY *request_key,
		SCEP_OPERATION request_type, SCEP_DATA **output)
{
	SCEP_ERROR error = SCEPE_OK;
	SCEP_DATA *local_out = malloc(sizeof(SCEP_DATA));
	if(!local_out) {
		error = SCEPE_MEMORY;
		goto finally;
	}
	memset(local_out, 0, sizeof(SCEP_DATA));

	error = scep_unwrap(
		handle, pkiMessage, request_cert, sig_cacert, request_key,
		&local_out);
	if(error != SCEPE_OK)
		goto finally;

	if(local_out->pkiStatus == SCEP_SUCCESS) {
		/* ensure type is correct */
		if(!PKCS7_type_is_signed(local_out->messageData))
			OSSL_ERR("Type of inner PKCS#7 must be signed (degenerate)");

		switch(request_type) {
			case SCEPOP_GETCACERT:
			case SCEPOP_PKCSREQ:
			case SCEPOP_GETCERT:
			case SCEPOP_GETNEXTCACERT:
			case SCEPOP_GETCERTINITIAL: ; // Small necessary hack
				/* ensure there are certs (at least 1) */
				STACK_OF(X509) *certs = local_out->messageData->d.sign->cert;
				if(sk_X509_num(certs) < 1)
					OSSL_ERR("Invalid number of certificates");

				/* set the output param */
				local_out->certs = certs;
				break;

			case SCEPOP_GETCRL: ; // hack again...
				/* ensure only one CRL */
				STACK_OF(X509_CRL) *crls = local_out->messageData->d.sign->crl;
				if(sk_X509_CRL_num(crls) != 1)
					OSSL_ERR("Invalid number of CRLs");

				/* set output param */
				local_out->crl = sk_X509_CRL_value(crls, 0);
				if(local_out->crl == NULL)
					OSSL_ERR("Unable to retrieve CRL from stack");
				break;

			default:
				error = SCEPE_UNKOWN_OPERATION;
				scep_log(handle, FATAL, "Invalid operation, cannot parse content");
				goto finally;
		}
	}

	*output = local_out;
finally:
	if(error != SCEPE_OK)
		free(local_out);
	return error;
}

/* Static helper methods for scep_unwrap
 * Reduces lines in scep_unwrap to keep things readable
 */

static SCEP_ERROR verify(
	SCEP *handle, PKCS7 *pkiMessage, X509 *sig_cacert, BIO **encData)
{
	SCEP_ERROR error = SCEPE_OK;
	BIO *data = NULL;

	/* prepare trusted store */
	X509_STORE *store = X509_STORE_new();
	if(!store)
		OSSL_ERR("Unable to create cert store");
	
	/* add trusted cert */
	X509_STORE_add_cert(store, sig_cacert);
	
	data = BIO_new(BIO_s_mem());
	if(!data)
		OSSL_ERR("Failed to create BIO for encrypted content");
	
	/* assuming cert is within pkiMessage */
	if (!PKCS7_verify(pkiMessage, NULL, store, NULL, data, 0))
		OSSL_ERR("verification failed");

	*encData = data;
finally:
	if(error != SCEPE_OK)
		if(data)
			BIO_free(data);
	if(store)
		X509_STORE_free(store);
	return error;
}

static SCEP_ERROR check_initial_enrollment(
	SCEP *handle, SCEP_DATA *data, X509 *signerCert)
{
	X509_NAME *subject, *issuer;
	SCEP_ERROR error = SCEPE_OK;
	data->initialEnrollment = 0;
	/* check for self-signed */
	issuer = X509_get_issuer_name(signerCert);
	if(!issuer)
		OSSL_ERR("Failed to extract issuer from certificate");
	subject = X509_get_subject_name(signerCert);
	if(!subject)
		OSSL_ERR("Failed to extract subject from certificate");

	if(X509_NAME_cmp(subject, issuer) == 0)
		data->initialEnrollment = 1;
finally:
	return error;
}

static SCEP_ERROR handle_certrep_attributes(
	SCEP *handle, SCEP_DATA *data, PKCS7_SIGNER_INFO *si)
{
	SCEP_ERROR error = SCEPE_OK;
	/* recipientNonce */
	ASN1_TYPE *recipientNonce = PKCS7_get_signed_attribute(si, handle->oids->recipientNonce);
	if(!recipientNonce)
		OSSL_ERR("recipient Nonce is missing");
	ASN1_TYPE_get_octetstring(recipientNonce, data->recipientNonce, NONCE_LENGTH);

	/* pkiStatus */
	ASN1_TYPE *pkiStatus = PKCS7_get_signed_attribute(si, handle->oids->pkiStatus);
	if(!pkiStatus)
		OSSL_ERR("PKI Status is missing");
	char *pki_status_str = (char *) ASN1_STRING_data(pkiStatus->value.printablestring);
	if(strncmp(pki_status_str, SCEP_PKISTATUS_SUCCESS, sizeof(SCEP_PKISTATUS_SUCCESS)) == 0)
		data->pkiStatus = SCEP_SUCCESS;
	else if(strncmp(pki_status_str, SCEP_PKISTATUS_FAILURE, sizeof(SCEP_PKISTATUS_FAILURE)) == 0)
		data->pkiStatus = SCEP_FAILURE;
	else if(strncmp(pki_status_str, SCEP_PKISTATUS_PENDING, sizeof(SCEP_PKISTATUS_PENDING)) == 0)
		data->pkiStatus = SCEP_PENDING;
	else {
		error = SCEPE_PROTOCOL;
		scep_log(handle, FATAL, "Invalid pkiStatus '%s'", pki_status_str);
		goto finally;
	}

	/*failInfo*/
	if(data->pkiStatus == SCEP_FAILURE) {
		ASN1_TYPE *failInfo = PKCS7_get_signed_attribute(si, handle->oids->failInfo);
		if(!failInfo)
			OSSL_ERR("failInfo is missing");
		char *failInfo_str = (char *) ASN1_STRING_data(failInfo->value.printablestring);
		if(strncmp(failInfo_str, SCEP_FAILINFO_BADALG, sizeof(SCEP_FAILINFO_BADALG)) == 0)
			data->failInfo = badAlg;
		else if(strncmp(failInfo_str, SCEP_FAILINFO_BADMESSAGECHECK, sizeof(SCEP_FAILINFO_BADMESSAGECHECK)) == 0)
			data->failInfo = badMessageCheck;
		else if(strncmp(failInfo_str, SCEP_FAILINFO_BADREQUEST, sizeof(SCEP_FAILINFO_BADREQUEST)) == 0)
			data->failInfo = badRequest;
		else if(strncmp(failInfo_str, SCEP_FAILINFO_BADTIME, sizeof(SCEP_FAILINFO_BADTIME)) == 0)
			data->failInfo = badTime;
		else if(strncmp(failInfo_str, SCEP_FAILINFO_BADCERTID, sizeof(SCEP_FAILINFO_BADCERTID)) == 0)
			data->failInfo = badCertId;
		else {
			error = SCEPE_PROTOCOL;
			scep_log(handle, FATAL, "Invalid failInfo '%s'", failInfo_str);
			goto finally;
		}
	}
finally:
	return error;
}

static SCEP_ERROR handle_encrypted_content(
	SCEP *handle, SCEP_DATA *data, PKCS7 *p7env, X509 *cacert, EVP_PKEY *cakey)
{
	SCEP_ERROR error = SCEPE_OK;
	BIO *decData = NULL;
	int ias_data_size;
	unsigned char *ias_data = NULL;

	/* Sort out invalid Certrep PENDING or FAILURE requests */
	if(data->messageType == SCEP_MSG_CERTREP && data->pkiStatus != SCEP_SUCCESS)
		OSSL_ERR("PENDING or FAILURE Certreps MUST NOT have encrypted content");

	/* Outer type must be enveloped */
	if(!PKCS7_type_is_enveloped(p7env))
		OSSL_ERR("Encrypted data is not enveoloped type");

	/* Perform checks on the enveoloped content */
	if(ASN1_INTEGER_get(p7env->d.enveloped->version) != 0)
		OSSL_ERR("Version of the enveloped part MUST be 0");

	/* Check inner content type is pkcs7-data */
	if(OBJ_obj2nid(p7env->d.enveloped->enc_data->content_type) != NID_pkcs7_data)
		OSSL_ERR("content-type of pkcs7envelope MUST be pkcs7-data");

	decData = BIO_new(BIO_s_mem());
	if(!decData)
		OSSL_ERR("Failed to allocate space for decryption BIO");

	if(!PKCS7_decrypt(p7env, cakey, cacert, decData, 0))
		OSSL_ERR("decryption failed");
	
	switch(data->messageType) {
		case SCEP_MSG_CERTREP:
			/* We set this as an intermediate value. messageData is only
			 * an internal field and supposed to be parsed into the
			 * correct field once scep_unwrap_response takes over.
			 */
			data->messageData = d2i_PKCS7_bio(decData, NULL);
			if(!data->messageData)
				OSSL_ERR("Not valid PKCS#7 after decryption for CertRep");
			break;
		case SCEP_MSG_PKCSREQ:
			data->request = NULL;
			/* Message type PKCSreq means there MUST be a CSR in it */
			d2i_X509_REQ_bio(decData, &(data->request));
			
			/* Validate CSR against SCEP requirements */

			if(!(X509_REQ_get_subject_name(data->request)))
				OSSL_ERR("The CSR MUST contain a Subject Distinguished Name");
			
			if(!(X509_REQ_get_pubkey(data->request)))
				OSSL_ERR("The CSR MUST contain a public key");
			
			int passwd_index = X509_REQ_get_attr_by_NID(data->request, NID_pkcs9_challengePassword, -1);
			if(passwd_index == -1)
				OSSL_ERR("The CSR MUST contain a challenge password");
			
			/* extract challenge password */
			X509_ATTRIBUTE *attr = X509_REQ_get_attr(data->request, passwd_index);
			if(attr->single == 0) { // set
				if(sk_ASN1_TYPE_num(attr->value.set) != 1)
					OSSL_ERR("Unexpected number of elements in challenge password");
				data->challenge_password = sk_ASN1_TYPE_value(attr->value.set, 0);
			} else { // single
				data->challenge_password = attr->value.single;
			}
			break;
		case SCEP_MSG_GETCERTINITIAL:
			ias_data_size = BIO_get_mem_data(decData, &ias_data);
			data->issuer_and_subject = d2i_PKCS7_ISSUER_AND_SUBJECT(NULL, (const unsigned char **) &ias_data, ias_data_size);
			if(!data->issuer_and_subject)
				OSSL_ERR("Unreadable Issuer and Subject data in encrypted content");
			break;
		case SCEP_MSG_GETCRL:
		case SCEP_MSG_GETCERT:
			ias_data_size = BIO_get_mem_data(decData, &ias_data);
			data->issuer_and_serial = d2i_PKCS7_ISSUER_AND_SERIAL(NULL, (const unsigned char **) &ias_data, ias_data_size);
			if(!data->issuer_and_serial)
				OSSL_ERR("Unreadable Issuer and Serial data in encrypted content");
			break;
	}

finally:
	if(decData)
		BIO_free(decData);
	return error;	
}

SCEP_ERROR scep_unwrap(
	SCEP *handle, PKCS7 *pkiMessage, X509 *cacert, X509 *sig_cacert, EVP_PKEY *cakey,
	SCEP_DATA **output)
{
	SCEP_DATA *local_out = NULL;
	SCEP_ERROR error = SCEPE_OK;
	STACK_OF(PKCS7_SIGNER_INFO)	*sk_si;
	PKCS7_SIGNER_INFO			*si;
	ASN1_TYPE					*messageType, *senderNonce, *transId;
	X509						*signerCert;
	STACK_OF(X509)				*certs;
	BIO							*encData = NULL;
	PKCS7 						*p7env;

	local_out = malloc(sizeof(SCEP_DATA));
	if(!local_out) {
		error = SCEPE_MEMORY;
		goto finally;
	}
	memset(local_out, 0, sizeof(SCEP_DATA));

	if(!PKCS7_type_is_signed(pkiMessage))
		OSSL_ERR("pkiMessage MUST be content type signed-data");

	/* Extract signer certificate from pkiMessage */
	certs = PKCS7_get0_signers(pkiMessage, NULL, 0);
	if(sk_X509_num(certs) < 1)
		OSSL_ERR("Signer certificate missing");
	if(sk_X509_num(certs) > 1) {
		if(handle->configuration->flags & SCEP_ALLOW_MULTIPLE_SIGNER_CERT) {
			scep_log(handle, WARN, "Multiple signer certs present. Ignoring as per configuration");
		} else {
			error = SCEPE_UNHANDLED;
			scep_log(handle, FATAL, "More than one signer certificate. Don't know how to handle this");
			goto finally;
		}
	}
	signerCert = sk_X509_value(certs, 0);

	/* Message type */
	if(!(sk_si = PKCS7_get_signer_info(pkiMessage)))
		 OSSL_ERR("Failed to get signer info");
	if(sk_PKCS7_SIGNER_INFO_num(sk_si) != 1)
		OSSL_ERR("Unexpected number of signer infos");
	if(!(si = sk_PKCS7_SIGNER_INFO_value(sk_si, 0)))
		 OSSL_ERR("Failed to get signer info value");
	if(!(messageType = PKCS7_get_signed_attribute(si, handle->oids->messageType)))
		OSSL_ERR("messageType is missing. Not a pkiMessage?");

	if (!ASN1_INTEGER_get(si->version) == 1)
		OSSL_ERR("version MUST be 1");

	/* luckily, standard defines single types */
	local_out->messageType_str = (char *) ASN1_STRING_data(messageType->value.printablestring);
	if(!local_out->messageType_str)
		OSSL_ERR("Failed to extract message type");

	/* Fill in integer-based type as well (redundant convenience field)
	 * Note: We check for each field specifically here as the number is limited.
	 * This avoids issues if someone sets weird values on this field that might
	 * confuse parsing functions like atoi or strtol. Also we can directly check
	 * for invalid message types.
	 */
	if(strncmp(local_out->messageType_str, SCEP_MSG_PKCSREQ_STR, sizeof(SCEP_MSG_PKCSREQ_STR)) == 0)
		local_out->messageType = SCEP_MSG_PKCSREQ;
	else if(strncmp(local_out->messageType_str, SCEP_MSG_CERTREP_STR, sizeof(SCEP_MSG_CERTREP_STR)) == 0)
		local_out->messageType = SCEP_MSG_CERTREP;
	else if(strncmp(local_out->messageType_str, SCEP_MSG_GETCERTINITIAL_STR, sizeof(SCEP_MSG_GETCERTINITIAL_STR)) == 0)
		local_out->messageType = SCEP_MSG_GETCERTINITIAL;
	else if(strncmp(local_out->messageType_str, SCEP_MSG_GETCERT_STR, sizeof(SCEP_MSG_GETCERT_STR)) == 0)
		local_out->messageType = SCEP_MSG_GETCERT;
	else if(strncmp(local_out->messageType_str, SCEP_MSG_GETCRL_STR, sizeof(SCEP_MSG_GETCRL_STR)) == 0)
		local_out->messageType = SCEP_MSG_GETCRL;
	else
		OSSL_ERR("Invalid messageType");

	/* Verification only happens when a trusted CA cert is present */
	if(sig_cacert) {
		error = verify(handle, pkiMessage, sig_cacert, &encData);
		if(error != SCEPE_OK)
			goto finally;
	}

	/* pkiMessage attributes
	 * First check for attributes common for all types, then specific fields.
	 */

	/* transaction ID */
	if(!(transId = PKCS7_get_signed_attribute(si, handle->oids->transId)))
		OSSL_ERR("transaction ID is missing");

	local_out->transactionID = (char *) ASN1_STRING_data(transId->value.printablestring);
	if(!local_out->transactionID)
		OSSL_ERR("Failed to extract transaction ID as string");

	/* senderNonce */
	if(!(senderNonce = PKCS7_get_signed_attribute(si, handle->oids->senderNonce)))
		OSSL_ERR("sender Nonce is missing");
	ASN1_TYPE_get_octetstring(senderNonce, local_out->senderNonce, NONCE_LENGTH);


	/* type-specific attributes */
	if(local_out->messageType == SCEP_MSG_CERTREP)
		error = handle_certrep_attributes(handle, local_out, si);
	else if(local_out->messageType == SCEP_MSG_PKCSREQ)
		error = check_initial_enrollment(handle, local_out, signerCert);

	/* for unhandled types this is SCEPE_OK anyway */
	if(error != SCEPE_OK)
		goto finally;

	/* If CA certificate & key are provided, decrypt enveloped message */
	if(cacert && cakey) {
		if((p7env = d2i_PKCS7_bio(encData, NULL))){
			error = handle_encrypted_content(handle, local_out, p7env, cacert, cakey);
			if(error != SCEPE_OK)
				goto finally;
		} else {
			/* Sort out any types which MUST contain encrypted data */
			if(local_out->messageType != SCEP_MSG_CERTREP || local_out->pkiStatus == SCEP_SUCCESS)
				OSSL_ERR("Message type requires an encrypted content");
		}
	}

	*output = local_out;

finally:
	if(error != SCEPE_OK)
		if(local_out)
			free(local_out);
	if(encData)
		BIO_free(encData);
	return error;

}

SCEP_ERROR make_degenP7(
 	SCEP *handle, X509 *cert, STACK_OF(X509) *additionalCerts, X509_CRL *crl, PKCS7 **p7)
{
	SCEP_ERROR error = SCEPE_OK;
	PKCS7 *local_p7 = NULL;
	PKCS7_SIGNED *p7s = NULL;
	STACK_OF(X509) *cert_stack = NULL;
	STACK_OF(X509_CRL) *crl_stack = NULL;
	X509 *currCert = NULL;
	int i;

	/*input validation*/
	if(!((cert == NULL) ^ (crl == NULL)))
		OSSL_ERR("cert and crl are mutually exclusive");

	/*quickly assemble degenerate pkcs7 signed-data structure*/
	if ((local_p7 = PKCS7_new()) == NULL)
        OSSL_ERR("could not create PKCS7 structure");
    if ((p7s = PKCS7_SIGNED_new()) == NULL)
        OSSL_ERR("could not create PKCS7 signed structure");
    local_p7->type = OBJ_nid2obj(NID_pkcs7_signed);
    local_p7->d.sign = p7s;
    /*TODO: not sure if this must be ommited, or if additionally empty content must be added*/
    p7s->contents->type = OBJ_nid2obj(NID_pkcs7_data);

    if (!ASN1_INTEGER_set(p7s->version, 1))
        OSSL_ERR("could not set version");

    if(cert) {
	    if ((cert_stack = sk_X509_new_null()) == NULL)
	    	OSSL_ERR("could create cert stack");

	    p7s->cert = cert_stack;

	    sk_X509_push(cert_stack, cert);

	    if(additionalCerts) {
		    for (i = 0; i = sk_X509_num(additionalCerts); i++) {
		    	currCert = sk_X509_shift(additionalCerts);
		    	sk_X509_push(cert_stack, currCert);
		    }
		}
	}
	else if(crl) {
		if ((crl_stack = sk_X509_CRL_new_null()) == NULL)
        	OSSL_ERR("could create crl stack");

        p7s->crl = crl_stack;
        sk_X509_CRL_push(crl_stack, crl);
	}

    *p7 = local_p7;

finally:
	return error;	
}