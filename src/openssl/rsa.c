/** 
 *
 * XMLSec library
 * 
 * RSA Algorithm support
 * 
 * See Copyright for the status of this software.
 * 
 * Author: Aleksey Sanin <aleksey@aleksey.com>
 */
#include "globals.h"

#ifndef XMLSEC_NO_RSA

#include <stdlib.h>
#include <string.h>

#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/objects.h>

#include <libxml/tree.h>

#include <xmlsec/xmlsec.h>
#include <xmlsec/keys.h>
#include <xmlsec/keysInternal.h>
#include <xmlsec/transforms.h>
#include <xmlsec/transformsInternal.h>
#include <xmlsec/digests.h>
#include <xmlsec/buffered.h>
#include <xmlsec/keyinfo.h>
#include <xmlsec/debug.h>
#include <xmlsec/errors.h>
#include <xmlsec/openssl/bn.h>
#include <xmlsec/openssl/evp.h>

/**
 * RSA transform
 */
static xmlSecTransformPtr xmlSecSignRsaSha1Create(xmlSecTransformId id);
static void 		xmlSecSignRsaSha1Destroy(xmlSecTransformPtr transform);
static int  		xmlSecSignRsaSha1AddKey	(xmlSecBinTransformPtr transform, 
						 xmlSecKeyPtr key);
static int 		xmlSecSignRsaSha1Update	(xmlSecDigestTransformPtr digest,
						 const unsigned char *buffer,
						 size_t size);
static int 		xmlSecSignRsaSha1Sign	(xmlSecDigestTransformPtr digest,
						 unsigned char **buffer,
						 size_t *size);
static int 		xmlSecSignRsaSha1Verify	(xmlSecDigestTransformPtr digest,
						 const unsigned char *buffer,
						 size_t size);

/**
 * RSA key
 */
static RSA* 		xmlSecRsaDup		(RSA *rsa); 
static xmlSecKeyPtr	xmlSecRsaKeyCreate	(xmlSecKeyId id);
static void		xmlSecRsaKeyDestroy	(xmlSecKeyPtr key);
static xmlSecKeyPtr	xmlSecRsaKeyDuplicate	(xmlSecKeyPtr key);
static int		xmlSecRsaKeyGenerate	(xmlSecKeyPtr key,
						 int keySize);
static int		xmlSecRsaKeySetValue	(xmlSecKeyPtr key,
						 void* data,
						 int dataSize);
static int		xmlSecRsaKeyRead	(xmlSecKeyPtr key,
						 xmlNodePtr node);
static int		xmlSecRsaKeyWrite	(xmlSecKeyPtr key,
						 xmlSecKeyType type,
						 xmlNodePtr parent);

struct _xmlSecKeyIdStruct xmlSecRsaKeyId = {
    /* xlmlSecKeyId data  */
    xmlSecRsaKeyValueName,		/* const xmlChar *keyValueNodeName; */
    xmlSecDSigNs, 			/* const xmlChar *keyValueNodeNs; */
    
    /* xmlSecKeyId methods */
    xmlSecRsaKeyCreate,		/* xmlSecKeyCreateMethod create; */    
    xmlSecRsaKeyDestroy,	/* xmlSecKeyDestroyMethod destroy; */
    xmlSecRsaKeyDuplicate,	/* xmlSecKeyDuplicateMethod duplicate; */
    xmlSecRsaKeyGenerate,	/* xmlSecKeyGenerateMethod generate; */
    xmlSecRsaKeySetValue,	/* xmlSecKeySetValueMethod setValue; */
    xmlSecRsaKeyRead, 		/* xmlSecKeyReadXmlMethod read; */
    xmlSecRsaKeyWrite,		/* xmlSecKeyWriteXmlMethod write; */
    NULL,			/* xmlSecKeyReadBinaryMethod readBin; */
    NULL			/* xmlSecKeyWriteBinaryMethod writeBin; */
};
xmlSecKeyId xmlSecRsaKey = &xmlSecRsaKeyId;



struct _xmlSecDigestTransformIdStruct xmlSecSignRsaSha1Id = {
    /* same as xmlSecTransformId */    
    xmlSecTransformTypeBinary,		/* xmlSecTransformType type; */
    xmlSecUsageDSigSignature,		/* xmlSecTransformUsage usage; */
    xmlSecSignRsaSha1Href, 		/* xmlChar *href; */
    
    xmlSecSignRsaSha1Create,		/* xmlSecTransformCreateMethod create; */
    xmlSecSignRsaSha1Destroy,		/* xmlSecTransformDestroyMethod destroy; */
    NULL,				/* xmlSecTransformReadNodeMethod read; */
    
    /* xmlSecBinTransform data/methods */
    &xmlSecRsaKeyId,
    xmlSecKeyTypePrivate,		/* xmlSecKeyType encryption; */
    xmlSecKeyTypePublic,		/* xmlSecKeyType decryption; */
    xmlSecBinTransformSubTypeDigest,	/* xmlSecBinTransformSubType binSubType; */
            
    xmlSecSignRsaSha1AddKey,		/* xmlSecBinTransformAddKeyMethod addBinKey; */
    xmlSecDigestTransformRead,		/* xmlSecBinTransformReadMethod readBin; */
    xmlSecDigestTransformWrite,		/* xmlSecBinTransformWriteMethod writeBin; */
    xmlSecDigestTransformFlush,		/* xmlSecBinTransformFlushMethod flushBin; */
    
    /* xmlSecDigestTransform data/methods */
    xmlSecSignRsaSha1Update,		/* xmlSecDigestUpdateMethod digestUpdate; */
    xmlSecSignRsaSha1Sign,		/* xmlSecDigestSignMethod digestSign; */
    xmlSecSignRsaSha1Verify		/* xmlSecDigestVerifyMethod digestVerify; */
};
xmlSecTransformId xmlSecSignRsaSha1 = (xmlSecTransformId)&xmlSecSignRsaSha1Id;

/**
 * RSA-PKCS1 
 */
static xmlSecTransformPtr xmlSecRsaPkcs1Create	(xmlSecTransformId id);
static void 	xmlSecRsaPkcs1Destroy		(xmlSecTransformPtr transform);
static int  	xmlSecRsaPkcs1AddKey		(xmlSecBinTransformPtr transform, 
						 xmlSecKeyPtr key);
static int  	xmlSecRsaPkcs1Process		(xmlSecBufferedTransformPtr buffered, 
						 xmlBufferPtr buffer);

static const struct _xmlSecBufferedTransformIdStruct xmlSecEncRsaPkcs1Id = {
    /* same as xmlSecTransformId */    
    xmlSecTransformTypeBinary,		/* xmlSecTransformType type; */
    xmlSecUsageEncryptionMethod,	/* xmlSecAlgorithmUsage usage; */
    xmlSecEncRsaPkcs1Href, 		/* const xmlChar href; */

    xmlSecRsaPkcs1Create, 		/* xmlSecTransformCreateMethod create; */
    xmlSecRsaPkcs1Destroy,		/* xmlSecTransformDestroyMethod destroy; */
    NULL,				/* xmlSecTransformReadMethod read; */
    
    /* binary data/methods */
    &xmlSecRsaKeyId,
    xmlSecKeyTypePublic,		/* xmlSecKeyType encryption; */
    xmlSecKeyTypePrivate,		/* xmlSecKeyType decryption; */
    xmlSecBinTransformSubTypeBuffered,
    xmlSecRsaPkcs1AddKey,		/* xmlSecBinTransformAddKeyMethod addBinKey; */
    xmlSecBufferedTransformRead,	/* xmlSecBinTransformReadMethod readBin; */
    xmlSecBufferedTransformWrite,	/* xmlSecBinTransformWriteMethod writeBin; */
    xmlSecBufferedTransformFlush,	/* xmlSecBinTransformFlushMethod flushBin; */
    
    /* xmlSecBufferedTransform data/methods */                                      
    xmlSecRsaPkcs1Process		/* xmlSecBufferedProcessMethod bufferedProcess; */
};
xmlSecTransformId xmlSecEncRsaPkcs1 = (xmlSecTransformId)&xmlSecEncRsaPkcs1Id;

/**
 * RSA-OAEP
 */
static xmlSecTransformPtr xmlSecRsaOaepCreate	(xmlSecTransformId id);
static void 	xmlSecRsaOaepDestroy		(xmlSecTransformPtr transform);
static int  	xmlSecRsaOaepAddKey		(xmlSecBinTransformPtr transform, 
						 xmlSecKeyPtr key);
static int 	xmlSecRsaOaepReadNode	 	(xmlSecTransformPtr transform,
						 xmlNodePtr transformNode);
static int  	xmlSecRsaOaepProcess		(xmlSecBufferedTransformPtr buffered, 
						 xmlBufferPtr buffer);

static const struct _xmlSecBufferedTransformIdStruct xmlSecEncRsaOaepId = {
    /* same as xmlSecTransformId */    
    xmlSecTransformTypeBinary,		/* xmlSecTransformType type; */
    xmlSecUsageEncryptionMethod,	/* xmlSecAlgorithmUsage usage; */
    xmlSecEncRsaOaepHref, 		/* const xmlChar href; */

    xmlSecRsaOaepCreate, 		/* xmlSecTransformCreateMethod create; */
    xmlSecRsaOaepDestroy,		/* xmlSecTransformDestroyMethod destroy; */
    xmlSecRsaOaepReadNode,		/* xmlSecTransformReadMethod read; */
    
    /* binary data/methods */
    &xmlSecRsaKeyId,
    xmlSecKeyTypePublic,		/* xmlSecKeyType encryption; */
    xmlSecKeyTypePrivate,		/* xmlSecKeyType decryption; */
    xmlSecBinTransformSubTypeBuffered,
    xmlSecRsaOaepAddKey,		/* xmlSecBinTransformAddKeyMethod addBinKey; */
    xmlSecBufferedTransformRead,	/* xmlSecBinTransformReadMethod readBin; */
    xmlSecBufferedTransformWrite,	/* xmlSecBinTransformWriteMethod writeBin; */
    xmlSecBufferedTransformFlush,	/* xmlSecBinTransformFlushMethod flushBin; */
    
    /* xmlSecBufferedTransform data/methods */                                      
    xmlSecRsaOaepProcess		/* xmlSecBufferedProcessMethod bufferedProcess; */
};
xmlSecTransformId xmlSecEncRsaOaep = (xmlSecTransformId)&xmlSecEncRsaOaepId;



#define xmlSecGetRsaKey( k ) 			((RSA*)(( k )->keyData))

/**
 * RSA-SHA1 transform
 */
#define XMLSEC_RSASHA1_TRANSFORM_SIZE \
    (sizeof(xmlSecDigestTransform) + sizeof(SHA_CTX))
#define xmlSecSignRsaSha1Context(t) \
    ((SHA_CTX*)(((xmlSecDigestTransformPtr)( t ))->digestData))
#define xmlSecSignRsaSha1ContextRsa(t) \
    ((RSA*)(((xmlSecDigestTransformPtr)( t ))->binData))


/**
 * xmlSecSignRsaSha1Create:
 */
static xmlSecTransformPtr 
xmlSecSignRsaSha1Create(xmlSecTransformId id) {
    xmlSecDigestTransformPtr digest;
    
    xmlSecAssert2(id != NULL, NULL);
        
    if(id != xmlSecSignRsaSha1){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecSignRsaSha1");
	return(NULL);
    }

    /*
     * Allocate a new xmlSecBinTransform and fill the fields.
     */
    digest = (xmlSecDigestTransformPtr) xmlMalloc(XMLSEC_RSASHA1_TRANSFORM_SIZE);
    if(digest == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "%d", XMLSEC_RSASHA1_TRANSFORM_SIZE);
	return(NULL);
    }
    memset(digest, 0, XMLSEC_RSASHA1_TRANSFORM_SIZE);
    
    digest->id = (xmlSecDigestTransformId)id;
    digest->digestData = ((unsigned char*)digest) + sizeof(xmlSecDigestTransform);

    SHA1_Init(xmlSecSignRsaSha1Context(digest)); 
    return((xmlSecTransformPtr)digest);
}

/**
 * xmlSecSignRsaSha1Destroy:
 */
static void 
xmlSecSignRsaSha1Destroy(xmlSecTransformPtr transform) {
    xmlSecDigestTransformPtr digest;

    xmlSecAssert(transform != NULL);
    
    if(!xmlSecTransformCheckId(transform, xmlSecSignRsaSha1)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecSignRsaSha1");
	return;
    }    
    digest = (xmlSecDigestTransformPtr)transform;

    if(xmlSecSignRsaSha1ContextRsa(transform) != NULL) {
	RSA_free(xmlSecSignRsaSha1ContextRsa(transform));
    }
    
    if(digest->digest != NULL) {
	memset(digest->digest, 0, digest->digestSize);
	xmlFree(digest->digest);
    }    
        
    memset(digest, 0, XMLSEC_RSASHA1_TRANSFORM_SIZE);
    xmlFree(digest);
}

/**
 * xmlSecSignRsaSha1Update:
 */
static int
xmlSecSignRsaSha1Update(xmlSecDigestTransformPtr digest,
			const unsigned char *buffer, size_t size) {
    xmlSecAssert2(digest != NULL, -1);
    
    if(!xmlSecTransformCheckId(digest, xmlSecSignRsaSha1)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecSignRsaSha1");
	return(-1);
    }    
    
    if((buffer == NULL) || (size == 0) || (digest->status != xmlSecTransformStatusNone)) {
	/* nothing to update */
	return(0);
    }
    
    SHA1_Update(xmlSecSignRsaSha1Context(digest), buffer, size); 
    return(0);
}

/**
 * xmlSecSignRsaSha1Sign:
 */
static int
xmlSecSignRsaSha1Sign(xmlSecDigestTransformPtr digest,
			unsigned char **buffer, size_t *size) {
    unsigned char buf[SHA_DIGEST_LENGTH]; 
    int ret;

    xmlSecAssert2(digest != NULL, -1);
    xmlSecAssert2(digest->digest != NULL, -1);
        
    if(!xmlSecTransformCheckId(digest, xmlSecSignRsaSha1) || 
      (xmlSecSignRsaSha1ContextRsa(digest) == NULL) ||
      ((xmlSecSignRsaSha1ContextRsa(digest)->d) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecSignRsaSha1");
	return(-1);
    }    
    if(digest->status != xmlSecTransformStatusNone) {
	return(0);
    }    
    SHA1_Final(buf, xmlSecSignRsaSha1Context(digest)); 
    
    ret = RSA_sign(NID_sha1, buf, SHA_DIGEST_LENGTH, 
		digest->digest, &(digest->digestSize), 
		xmlSecSignRsaSha1ContextRsa(digest));
    if(ret != 1) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    "RSA_sign - %d", ret);
	return(-1);	    
    }
    
    if(buffer != NULL) {
	(*buffer) = digest->digest;
    }        
    if(size != NULL) {
	(*size) = digest->digestSize;
    }        
    digest->status = xmlSecTransformStatusOk;
    return(0);
}

/**
 * xmlSecSignRsaSha1Verify:
 */
static int
xmlSecSignRsaSha1Verify(xmlSecDigestTransformPtr digest,
			const unsigned char *buffer, size_t size) {
    unsigned char buf[SHA_DIGEST_LENGTH]; 
    int ret;

    xmlSecAssert2(digest != NULL, -1);
    xmlSecAssert2(buffer != NULL, -1);
        
    if(!xmlSecTransformCheckId(digest, xmlSecSignRsaSha1) ||
       (xmlSecSignRsaSha1ContextRsa(digest) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecSignRsaSha1");
	return(-1);
    }    

    SHA1_Final(buf, xmlSecSignRsaSha1Context(digest)); 
    
    ret = RSA_verify(NID_sha1, buf, SHA_DIGEST_LENGTH, 
		     (unsigned char *)buffer, size, 
		     xmlSecSignRsaSha1ContextRsa(digest));
    if(ret == 1) {
	digest->status = xmlSecTransformStatusOk;
    } else if(ret == 0) {
	digest->status = xmlSecTransformStatusFail;
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    "RSA_verify - %d", ret);
	return(-1);
    }
    
    return(0);
}

/**
 * xmlSecSignRsaSha1AddKey:
 */																 
static int
xmlSecSignRsaSha1AddKey	(xmlSecBinTransformPtr transform, xmlSecKeyPtr key) {
    xmlSecDigestTransformPtr digest;
    RSA *rsa;
    void *digestBuf;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(key != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecSignRsaSha1) || 
       !xmlSecKeyCheckId(key, xmlSecRsaKey) || 
       (xmlSecGetRsaKey(key) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM_OR_KEY,
		    "xmlSecSignRsaSha1 and xmlSecRsaKey");
	return(-1);
    }    
    digest = (xmlSecDigestTransformPtr)transform;

    rsa = xmlSecRsaDup(xmlSecGetRsaKey(key));
    if(rsa == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecRsaDup");
	return(-1);
    }

    digestBuf = xmlMalloc(sizeof(unsigned char) * RSA_size(rsa));
    if(digestBuf == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "%d", sizeof(unsigned char) * RSA_size(rsa));
	RSA_free(rsa);
	return(-1);
    }
    
    if(digest->digest != NULL) {
	memset(digest->digest, 0, digest->digestSize);
	xmlFree(digest->digest);  
    }    
    digest->digest = digestBuf;
    digest->digestSize = RSA_size(rsa);
        
    if(xmlSecSignRsaSha1ContextRsa(transform) != NULL) {
	RSA_free(xmlSecSignRsaSha1ContextRsa(transform));
    }    
    transform->binData = rsa;
    return(0);
}

/***********************************************************************
 *
 * RSA key
 *
 **********************************************************************/
static 
RSA* xmlSecRsaDup(RSA *rsa) {
    RSA *newRsa;
    
    xmlSecAssert2(rsa != NULL, NULL);

    /* increment reference counter instead of coping if possible */
#ifndef XMLSEC_OPENSSL096
    RSA_up_ref(rsa);
    newRsa =  rsa;
#else /* XMLSEC_OPENSSL096 */     
    
    newRsa = RSA_new();
    if(newRsa == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    "RSA_new");
	return(NULL);
    }

    if(rsa->n != NULL) {
	newRsa->n = BN_dup(rsa->n);
    }
    if(rsa->e != NULL) {
	newRsa->e = BN_dup(rsa->e);
    }
    if(rsa->d != NULL) {
	newRsa->d = BN_dup(rsa->d);
    }
#endif /* XMLSEC_OPENSSL096 */     
    return(newRsa);
}
 
/**
 * xmlSecRsaKeyCreate:
 */
static xmlSecKeyPtr	
xmlSecRsaKeyCreate(xmlSecKeyId id) {
    xmlSecKeyPtr key;
    
    xmlSecAssert2(id != NULL, NULL);
    
    if(id != xmlSecRsaKey) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	return(NULL);	
    }
    
    key = (xmlSecKeyPtr)xmlMalloc(sizeof(struct _xmlSecKey));
    if(key == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(struct _xmlSecKey)=%d", 
		    sizeof(struct _xmlSecKey));
	return(NULL);
    }
    memset(key, 0, sizeof(struct _xmlSecKey));  
        
    key->id = id;
    return(key);
}

/**
 * xmlSecRsaKeyDestroy:
 */
static void
xmlSecRsaKeyDestroy(xmlSecKeyPtr key) {
    xmlSecAssert(key != NULL);

    if(!xmlSecKeyCheckId(key, xmlSecRsaKey)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	return;
    }
    
    if(xmlSecGetRsaKey(key) != NULL) {
	RSA_free(xmlSecGetRsaKey(key));
    }    
    memset(key, 0, sizeof(struct _xmlSecKey));
    
    xmlFree(key);		    
}

static xmlSecKeyPtr	
xmlSecRsaKeyDuplicate(xmlSecKeyPtr key) {
    xmlSecKeyPtr newKey;

    xmlSecAssert2(key != NULL, NULL);
    
    if(!xmlSecKeyCheckId(key, xmlSecRsaKey)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	return(NULL);
    }
    
    newKey = xmlSecRsaKeyCreate(key->id);
    if(newKey == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecRsaKeyCreate");
	return(NULL);
    }
    
    if(xmlSecGetRsaKey(key) != NULL) {
	newKey->keyData = xmlSecRsaDup(xmlSecGetRsaKey(key));
	if(newKey->keyData == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecRsaDup");
	    xmlSecKeyDestroy(newKey);
	    return(NULL);    
	}
	if(xmlSecGetRsaKey(newKey)->d != NULL) {
	    newKey->type = xmlSecKeyTypePrivate;
	} else {
	    newKey->type = xmlSecKeyTypePublic;
	}
    }
    return(newKey);
}

static int
xmlSecRsaKeyGenerate(xmlSecKeyPtr key, int keySize) {
    RSA *rsa;
    int ret;

    xmlSecAssert2(key != NULL, -1);
    
    if(!xmlSecKeyCheckId(key, xmlSecRsaKey)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	return(-1);
    }

    /* todo: change exponent? */
    rsa = RSA_generate_key(keySize, 3, NULL, NULL); 
    if(rsa == NULL) {
        xmlSecError(XMLSEC_ERRORS_HERE,
    		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    "RSA_generate_key");
	return(-1);
    }    
    
    ret = xmlSecRsaKeySetValue(key, rsa, sizeof(RSA));
    if(ret < 0) {
        xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecRsaKeySetValue");
	RSA_free(rsa);
	return(-1);
    }
    RSA_free(rsa);
    return(0);
}

static int	
xmlSecRsaKeySetValue(xmlSecKeyPtr key, void* data, int dataSize) {
    RSA* rsa = NULL;
    xmlSecAssert2(key != NULL, -1);
    
    if(!xmlSecKeyCheckId(key, xmlSecRsaKey)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	return(-1);
    }
    
    /* not the best way to do it but... */
    if(dataSize != sizeof(RSA)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY_DATA,
		    "%d bytes actually, %d bytes expected", dataSize, sizeof(RSA));
	return(-1);
    }

    if(data != NULL) {
	rsa = xmlSecRsaDup((RSA*)data); 
	if(rsa == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecRsaDup");
	    return(-1);    
	}
    }
    if(xmlSecGetRsaKey(key) != NULL) {
	RSA_free(xmlSecGetRsaKey(key));
    }    
    key->keyData = rsa;
    if((rsa != NULL) && (rsa->d != NULL)) {
        key->type = xmlSecKeyTypePrivate;    
    } else {
        key->type = xmlSecKeyTypePublic;    
    }
    return(0);
}

static int
xmlSecRsaKeyRead(xmlSecKeyPtr key, xmlNodePtr node) {
    unsigned char* modValue = NULL; size_t modSize = 0;
    unsigned char* expValue = NULL; size_t expSize = 0;
    unsigned char* privExpValue = NULL; size_t privExpSize = 0;
    RSA *rsa = NULL;
    int res = -1; /* by default we fail */
    int ret;
    
    xmlSecAssert2(key != NULL, -1);
    xmlSecAssert2(node != NULL, -1);

    if(!xmlSecKeyCheckId(key, xmlSecRsaKey)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	goto done;
    }
    
    /* get data from xml node */
    ret = xmlSecKeyInfoReadRSAKeyValueNode(node, 
			&modValue, &modSize, &expValue, &expSize,
			&privExpValue, &privExpSize);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecKeyInfoReadRSAKeyValueNode - %d", ret);
	goto done;
    }

    /* and push to RSA structure */
    rsa = RSA_new();
    if(rsa == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    "RSA_new");
	goto done;
    }
    
    /* Modulus is required */
    if(xmlSecBnFromCryptoBinary(modValue, modSize, &(rsa->n)) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBnFromCryptoBinary - %d (Modulus)", ret);
	goto done;
    }
    /* Exponent is required */
    if(xmlSecBnFromCryptoBinary(expValue, expSize, &(rsa->e)) == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBnFromCryptoBinary - %d (Exponent)", ret);
	goto done;
    }
    /* PrivateExponent is required for private keys only*/
    if(privExpValue != NULL) {
	if(xmlSecBnFromCryptoBinary(privExpValue, privExpSize, &(rsa->d)) == NULL) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecBnFromCryptoBinary - %d (PrivateExponent)", ret);
	    goto done;
	}
    }

    ret = xmlSecKeySetValue(key, rsa, sizeof(RSA));
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecKeySetValue - %d", ret);
	goto done;
    }
    res = 0; /* success! */

done:
    /* cleanup everything we've allocated */
    if(modValue != NULL) {
	xmlFree(modValue);
    }
    if(expValue != NULL) {
	xmlFree(expValue);
    }
    if(privExpValue != NULL) {
	xmlFree(privExpValue);
    }
    if(rsa != NULL) {
	RSA_free(rsa);
    }
    return(res);
}


/**
 * xmlSecRsaKeyWrite:
 */
static int
xmlSecRsaKeyWrite(xmlSecKeyPtr key, xmlSecKeyType type, xmlNodePtr parent) {
    unsigned char* modValue = NULL; size_t modSize = 0;
    unsigned char* expValue = NULL; size_t expSize = 0;
    unsigned char* privExpValue = NULL; size_t privExpSize = 0;
    int ret;
    int res = -1;
    
    xmlSecAssert2(key != NULL, -1);
    xmlSecAssert2(parent != NULL, -1);

    if(!xmlSecKeyCheckId(key, xmlSecRsaKey) || (xmlSecGetRsaKey(key) == NULL)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_KEY,
		    "xmlSecRsaKey");
	goto done;
    }
    
    /* Modulus */
    ret = xmlSecBnToCryptoBinary(xmlSecGetRsaKey(key)->n, &modValue, &modSize);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBnToCryptoBinary - %d (Modulus - n)", ret);
	goto done;
    }

    /* Exponent */
    ret = xmlSecBnToCryptoBinary(xmlSecGetRsaKey(key)->e, &expValue, &expSize);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecBnToCryptoBinary - %d (Exponent - e)", ret);
	goto done;
    }

    /* PrivateExponent */
    if((type == xmlSecKeyTypePrivate) || 
       ((type == xmlSecKeyTypeAny) && (xmlSecGetRsaKey(key)->d != NULL))) {

	ret = xmlSecBnToCryptoBinary(xmlSecGetRsaKey(key)->d, &privExpValue, &privExpSize);
	if(ret < 0) {
	    xmlSecError(XMLSEC_ERRORS_HERE,
			XMLSEC_ERRORS_R_XMLSEC_FAILED,
			"xmlSecBnToCryptoBinary - %d (PrivateExponent - d)", ret);
	    goto done;
	}
    }

    /* write the xml */
    ret = xmlSecKeyInfoWriteRSAKeyValueNode(parent, 
			modValue, modSize, expValue, expSize,
			privExpValue, privExpSize);
    if(ret < 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecKeyInfoWriteRSAKeyValueNode - %d", ret);
	goto done;
    }
    res = 0; /* success! */

done:
    /* cleanup everything we've allocated */
    if(modValue != NULL) {
	xmlFree(modValue);
    }
    if(expValue != NULL) {
	xmlFree(expValue);
    }
    if(privExpValue != NULL) {
	xmlFree(privExpValue);
    }
    return(res);
}


/**************************************************************************
 *
 * RSA-PKCS1 
 *
 **************************************************************************/
#define xmlSecRsaPkcs1Rsa(t) \
    ((RSA*)(((xmlSecBufferedTransformPtr)( t ))->binData))
    
static xmlSecTransformPtr 
xmlSecRsaPkcs1Create(xmlSecTransformId id) {
    xmlSecBufferedTransformPtr buffered;

    xmlSecAssert2(id != NULL, NULL);
    
    if(id != xmlSecEncRsaPkcs1){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaPkcs1");
	return(NULL);
    }

    /*
     * Allocate a new xmlSecBufferedTransform and fill the fields.
     */
    buffered = (xmlSecBufferedTransformPtr)xmlMalloc(sizeof(xmlSecBufferedTransform));
    if(buffered == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecBufferedTransform)=%d",
		    sizeof(xmlSecBufferedTransform));
	return(NULL);
    }
    memset(buffered, 0, sizeof(xmlSecBufferedTransform));
    
    buffered->id = (xmlSecBufferedTransformId)id;
    return((xmlSecTransformPtr)buffered);
}

static void 	
xmlSecRsaPkcs1Destroy(xmlSecTransformPtr transform) {
    xmlSecBufferedTransformPtr buffered;
    
    xmlSecAssert(transform != NULL);

    if(!xmlSecTransformCheckId(transform, xmlSecEncRsaPkcs1)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaPkcs1");
	return;
    }    
    buffered = (xmlSecBufferedTransformPtr)transform;

    if(xmlSecRsaPkcs1Rsa(buffered) != NULL) {
	RSA_free(xmlSecRsaPkcs1Rsa(buffered));
    }    
    xmlSecBufferedDestroy(buffered);        
    memset(buffered, 0, sizeof(xmlSecBufferedTransform));
    xmlFree(buffered);
}

static int
xmlSecRsaPkcs1AddKey(xmlSecBinTransformPtr transform, xmlSecKeyPtr key) {
    xmlSecBufferedTransformPtr buffered;
    RSA *rsa;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(key != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecEncRsaPkcs1) || 
       !xmlSecKeyCheckId(key, xmlSecRsaKey) || 
       (xmlSecGetRsaKey(key) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM_OR_KEY,
		    "xmlSecEncRsaPkcs1 and xmlSecRsaKey");
	return(-1);
    }    
    buffered = (xmlSecBufferedTransformPtr)transform;

    rsa = xmlSecRsaDup(xmlSecGetRsaKey(key)); 
    if(rsa == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecRsaDup");
	return(-1);    
    }
        
    if(xmlSecRsaPkcs1Rsa(buffered) != NULL) {
	RSA_free(xmlSecRsaPkcs1Rsa(buffered));
    }    
    transform->binData = rsa;
    return(0);
}

static int
xmlSecRsaPkcs1Process(xmlSecBufferedTransformPtr buffered,  xmlBufferPtr buffer) {
    size_t size;
    int ret;    

    xmlSecAssert2(buffered != NULL, -1);
    xmlSecAssert2(buffer != NULL, -1);
    
    if(!xmlSecTransformCheckId(buffered, xmlSecEncRsaPkcs1) ||
       (xmlSecRsaPkcs1Rsa(buffered) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaPkcs1");
	return(-1);
    }    

    size = xmlBufferLength(buffer);
    if(buffered->encode) {
	xmlBufferResize(buffer, RSA_size(xmlSecRsaPkcs1Rsa(buffered)));
	ret = RSA_public_encrypt(size, xmlBufferContent(buffer),
				 (unsigned char*)xmlBufferContent(buffer), 
				 xmlSecRsaPkcs1Rsa(buffered),
				 RSA_PKCS1_PADDING);
    } else if(size == (size_t)RSA_size(xmlSecRsaPkcs1Rsa(buffered))) {
	ret = RSA_private_decrypt(size, xmlBufferContent(buffer),
				 (unsigned char*)xmlBufferContent(buffer), 
				 xmlSecRsaPkcs1Rsa(buffered),
				 RSA_PKCS1_PADDING);
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_SIZE,
		    "%d", size);
	return(-1);	
    }
    if(ret <= 0) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_CRYPTO_FAILED,
		    (buffered->encode) ? "RSA_public_encrypt" : "RSA_private_decrypt");
	return(-1);	
    }
    buffer->use = ret;
    return(0);
}

/***************************************************************************
 *
 * RSA-OAEP
 *
 ***************************************************************************/
#define xmlSecRsaOaepRsa(t) \
    ((RSA*)(((xmlSecBufferedTransformPtr)( t ))->binData))
    
static xmlSecTransformPtr 
xmlSecRsaOaepCreate(xmlSecTransformId id) {
    xmlSecBufferedTransformPtr buffered;

    xmlSecAssert2(id != NULL, NULL);
    
    if(id != xmlSecEncRsaOaep){
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaOaep");
	return(NULL);
    }

    /*
     * Allocate a new xmlSecBufferedTransform and fill the fields.
     */
    buffered = (xmlSecBufferedTransformPtr)xmlMalloc(sizeof(xmlSecBufferedTransform));
    if(buffered == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_MALLOC_FAILED,
		    "sizeof(xmlSecBufferedTransform)=%d",
		    sizeof(xmlSecBufferedTransform));
	return(NULL);
    }
    memset(buffered, 0, sizeof(xmlSecBufferedTransform));
    
    buffered->id = (xmlSecBufferedTransformId)id;
    return((xmlSecTransformPtr)buffered);
}

static void 	
xmlSecRsaOaepDestroy(xmlSecTransformPtr transform) {
    xmlSecBufferedTransformPtr buffered;

    xmlSecAssert(transform != NULL);
    
    if(!xmlSecTransformCheckId(transform, xmlSecEncRsaOaep)) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaOaep");
	return;
    }    
    buffered = (xmlSecBufferedTransformPtr)transform;

    if(xmlSecRsaOaepRsa(buffered) != NULL) {
	RSA_free(xmlSecRsaOaepRsa(buffered));
    } 
    if(buffered->data != NULL) {
	xmlBufferFree((xmlBufferPtr)buffered->data);
    }   
    xmlSecBufferedDestroy(buffered);        
    memset(buffered, 0, sizeof(xmlSecBufferedTransform));
    xmlFree(buffered);
}

static int 	
xmlSecRsaOaepReadNode(xmlSecTransformPtr transform, xmlNodePtr transformNode) {
    xmlSecBufferedTransformPtr buffered;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(transformNode != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecEncRsaOaep) ) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaOaep");
	return(-1);
    }    
    buffered = (xmlSecBufferedTransformPtr)transform;
    
    /* TODO: */
    return(0);
}


static int
xmlSecRsaOaepAddKey(xmlSecBinTransformPtr transform, xmlSecKeyPtr key) {
    xmlSecBufferedTransformPtr buffered;
    RSA *rsa;

    xmlSecAssert2(transform != NULL, -1);
    xmlSecAssert2(key != NULL, -1);
    
    if(!xmlSecTransformCheckId(transform, xmlSecEncRsaOaep) || 
       !xmlSecKeyCheckId(key, xmlSecRsaKey) ||
       (xmlSecGetRsaKey(key) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM_OR_KEY,
		    "xmlSecEncRsaOaep and xmlSecRsaKey");
	return(-1);
    }    
    buffered = (xmlSecBufferedTransformPtr)transform;

    rsa = xmlSecRsaDup(xmlSecGetRsaKey(key));
    if(rsa == NULL) {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_XMLSEC_FAILED,
		    "xmlSecRsaDup");
	return(-1);
    }
    transform->binData = rsa;
    return(0);
}

static int
xmlSecRsaOaepProcess(xmlSecBufferedTransformPtr buffered,  xmlBufferPtr buffer) {
    size_t size;
    int rsa_size = 0;
    int ret;    
    RSA *rsa;

    xmlSecAssert2(buffered != NULL, -1);
    xmlSecAssert2(buffer != NULL, -1);
    
    if(!xmlSecTransformCheckId(buffered, xmlSecEncRsaOaep) ||
        (xmlSecRsaOaepRsa(buffered) == NULL)) {

	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_TRANSFORM,
		    "xmlSecEncRsaOaep");
	return(-1);
    }    
    rsa = xmlSecRsaOaepRsa(buffered);
    rsa_size = RSA_size(rsa);
    size = xmlBufferLength(buffer);
    if(buffered->encode) {
	xmlBufferResize(buffer, rsa_size);
	
	if(buffered->data == NULL) {    
	    /* 
	     * simple case: OAEPparams not specified
	     * we can use standard OpenSSL function
	     */
    	    ret = RSA_public_encrypt(size, xmlBufferContent(buffer),  
	                           (unsigned char*)xmlBufferContent(buffer), 
				   rsa, RSA_PKCS1_OAEP_PADDING); 
	    if(ret <= 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_public_encrypt - %d", ret);
		return(-1);	
	    }
	} else {
	    ret = RSA_padding_add_PKCS1_OAEP(
			    (unsigned char*)xmlBufferContent(buffer), rsa_size, 
			    xmlBufferContent(buffer), size,
			    xmlBufferContent((xmlBufferPtr)buffered->data), 
			    xmlBufferLength((xmlBufferPtr)buffered->data));
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_padding_add_PKCS1_OAEP - %d", ret);
		return(-1);
	    }	
	    ret = RSA_public_encrypt(rsa_size, xmlBufferContent(buffer),
				 (unsigned char*)xmlBufferContent(buffer), 
				 rsa, RSA_NO_PADDING);
	    if(ret <= 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_public_encrypt - %d", ret);
		return(-1);	
	    }
	}
    } else if(size == (size_t)rsa_size) {
	
	if(buffered->data == NULL) {    
	    /* 
	     * simple case: OAEPparams not specified
	     * we can use standard OpenSSL function
	     */
    	    ret = RSA_private_decrypt(size, xmlBufferContent(buffer),  
	                           (unsigned char*)xmlBufferContent(buffer), 
				   rsa, RSA_PKCS1_OAEP_PADDING); 
	    if(ret <= 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_private_decrypt - %d", ret);
		return(-1);	
	    }
	} else {
	    BIGNUM bn;
	
	    ret = RSA_private_decrypt(size, xmlBufferContent(buffer),
				 (unsigned char*)xmlBufferContent(buffer), 
				 rsa, RSA_NO_PADDING);
	    if(ret <= 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_private_decrypt - %d", ret);
		return(-1);	
	    }
	
	    /* 
    	     * the private decrypt w/o padding adds '0's at the begginning.
	     * it's not clear for me can I simply skip all '0's from the
	     * beggining so I have to do decode it back to BIGNUM and dump
	     * buffer again
	     */
	    BN_init(&bn);
	    if(BN_bin2bn(xmlBufferContent(buffer), ret, &bn) == NULL) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "BN_bin2bn");
		return(-1);		    
	    }
	    ret = BN_bn2bin(&bn, (unsigned char*)xmlBufferContent(buffer));
	    BN_clear_free(&bn);
	
	    ret = RSA_padding_check_PKCS1_OAEP(
			    (unsigned char*)xmlBufferContent(buffer), size, 
			    xmlBufferContent(buffer), ret, rsa_size,
			    xmlBufferContent((xmlBufferPtr)buffered->data), 
			    xmlBufferLength((xmlBufferPtr)buffered->data));
	    if(ret < 0) {
		xmlSecError(XMLSEC_ERRORS_HERE,
			    XMLSEC_ERRORS_R_CRYPTO_FAILED,
			    "RSA_padding_check_PKCS1_OAEP - %d", ret);
		return(-1);
	    }
	}				    
    } else {
	xmlSecError(XMLSEC_ERRORS_HERE,
		    XMLSEC_ERRORS_R_INVALID_SIZE,
		    "size %d != rsa size %d", size, rsa_size);
	return(-1);	
    }
    buffer->use = ret;
    return(0);
}


#endif /* XMLSEC_NO_RSA */




