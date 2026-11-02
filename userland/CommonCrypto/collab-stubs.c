/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 The Blueberry Pie OS Authors
 */
#include <CommonCrypto/CommonCollabKeyGen.h>
#include <CommonCrypto/CommonCryptoError.h>
#include <stddef.h>

int
CCCKGGetCommitmentSize(size_t nbits, CCDigestAlgorithm alg)
{
    (void)nbits;
    (void)alg;
    return 0;
}

int
CCCKGGetShareSize(size_t nbits, CCDigestAlgorithm alg)
{
    (void)nbits;
    (void)alg;
    return 0;
}

int
CCCKGGetOpeningSize(size_t nbits, CCDigestAlgorithm alg)
{
    (void)nbits;
    (void)alg;
    return 0;
}

CCCryptorStatus
CCCKGContributorCreate(size_t nbits, CCDigestAlgorithm alg, CCCollabKeyGenContributorRef *contrib)
{
    (void)nbits;
    (void)alg;
    if (contrib == NULL) {
        return kCCParamError;
    }
    *contrib = NULL;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKGOwnerCreate(size_t nbits, CCDigestAlgorithm alg, CCCollabKeyGenOwnerRef *owner)
{
    (void)nbits;
    (void)alg;
    if (owner == NULL) {
        return kCCParamError;
    }
    *owner = NULL;
    return kCCUnimplemented;
}

void
CCCKGContributorDestroy(CCCollabKeyGenContributorRef contrib)
{
    (void)contrib;
}

void
CCCKGOwnerDestroy(CCCollabKeyGenOwnerRef owner)
{
    (void)owner;
}

CCCryptorStatus
CCCKGContributorCommit(CCCollabKeyGenContributorRef contrib, void *commitment, size_t commitmentLen)
{
    (void)contrib;
    (void)commitment;
    (void)commitmentLen;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKGOwnerGenerateShare(CCCollabKeyGenOwnerRef owner,
                        const void *commitment, size_t commitmentLen,
                        void *share, size_t shareLen)
{
    (void)owner;
    (void)commitment;
    (void)commitmentLen;
    (void)share;
    (void)shareLen;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKGContributorFinish(CCCollabKeyGenContributorRef contrib,
                       const void *share, size_t shareLen,
                       void *opening, size_t openingLen,
                       void *sharedKey, size_t sharedKeyLen,
                       CCECCryptorRef *publicKey)
{
    (void)contrib;
    (void)share;
    (void)shareLen;
    (void)opening;
    (void)openingLen;
    (void)sharedKey;
    (void)sharedKeyLen;
    (void)publicKey;
    if (publicKey != NULL) {
        *publicKey = NULL;
    }
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKGOwnerFinish(CCCollabKeyGenOwnerRef owner,
                 const void *opening, size_t openingLen,
                 void *sharedKey, size_t sharedKeyLen,
                 CCECCryptorRef *privateKey)
{
    (void)owner;
    (void)opening;
    (void)openingLen;
    (void)sharedKey;
    (void)sharedKeyLen;
    (void)privateKey;
    if (privateKey != NULL) {
        *privateKey = NULL;
    }
    return kCCUnimplemented;
}

CCCKG2Params
CCCKG2ParamsP224Sha256Version2(void)
{
    return NULL;
}

int
CCCKG2GetCommitmentSize(CCCKG2Params params)
{
    (void)params;
    return 0;
}

int
CCCKG2GetShareSize(CCCKG2Params params)
{
    (void)params;
    return 0;
}

int
CCCKG2GetOpeningSize(CCCKG2Params params)
{
    (void)params;
    return 0;
}

CCCryptorStatus
CCCKG2ContributorCreate(CCCKG2Params params, CCCKG2ContributorRef *contrib)
{
    (void)params;
    if (contrib == NULL) {
        return kCCParamError;
    }
    *contrib = NULL;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKG2OwnerCreate(CCCKG2Params params, CCCKG2OwnerRef *owner)
{
    (void)params;
    if (owner == NULL) {
        return kCCParamError;
    }
    *owner = NULL;
    return kCCUnimplemented;
}

void
CCCKG2ContributorDestroy(CCCKG2ContributorRef contrib)
{
    (void)contrib;
}

void
CCCKG2OwnerDestroy(CCCKG2OwnerRef owner)
{
    (void)owner;
}

CCCryptorStatus
CCCKG2ContributorCommit(CCCKG2ContributorRef contrib, void *commitment, size_t commitmentLen)
{
    (void)contrib;
    (void)commitment;
    (void)commitmentLen;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKG2OwnerGenerateShare(CCCKG2OwnerRef owner,
                         const void *commitment, size_t commitmentLen,
                         void *share, size_t shareLen)
{
    (void)owner;
    (void)commitment;
    (void)commitmentLen;
    (void)share;
    (void)shareLen;
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKG2ContributorFinish(CCCKG2ContributorRef contrib,
                        const void *share, size_t shareLen,
                        void *opening, size_t openingLen,
                        void *sharedKey, size_t sharedKeyLen,
                        CCECCryptorRef *publicKey)
{
    (void)contrib;
    (void)share;
    (void)shareLen;
    (void)opening;
    (void)openingLen;
    (void)sharedKey;
    (void)sharedKeyLen;
    (void)publicKey;
    if (publicKey != NULL) {
        *publicKey = NULL;
    }
    return kCCUnimplemented;
}

CCCryptorStatus
CCCKG2OwnerFinish(CCCKG2OwnerRef owner,
                  const void *opening, size_t openingLen,
                  void *sharedKey, size_t sharedKeyLen,
                  CCECCryptorRef *privateKey)
{
    (void)owner;
    (void)opening;
    (void)openingLen;
    (void)sharedKey;
    (void)sharedKeyLen;
    (void)privateKey;
    if (privateKey != NULL) {
        *privateKey = NULL;
    }
    return kCCUnimplemented;
}
