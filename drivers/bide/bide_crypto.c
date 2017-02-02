/*
 * Copyright (C) 2014 BlackBerry Limited
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Kernel Includes */
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/sha.h>

/* Local Includes */
#include "bide.h"
#include "bide_internal.h"
#include "bide_log.h"

/*************************************************************************/

/*
 * This function initializes the crypto library for processing.
 *
 * @param   alg                 The hashing algorithm to use.
 * @param   ctx                 The crypto context that all funcitons need.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_begin(unsigned alg,
		 struct crypto_context *ctx)
{
	static const char *algs[] = { "sha256", "sha512" };
	int rc = 0;

	if (!ctx || alg > HASH_ALG_LAST)
		return -EINVAL;

	/* Request hashing algo */
	ctx->desc.tfm = crypto_alloc_hash(algs[alg], 0, 0);
	if (IS_ERR(ctx->desc.tfm)) {
		rc = (int) PTR_ERR(ctx->desc.tfm);

		logError("Failed on crypto_alloc_hash(%s). rc=%d.", algs[alg], -rc);
		return rc;
	}

	/* Initialize hashing algo */
	rc = crypto_hash_init(&ctx->desc);
	if (rc) {
		logError("Failed on crypto_hash_init(%s). rc=%d.", algs[alg],  -rc);
		return rc;
	}

	return 0;
}

/*************************************************************************/

/*
 * Adds more data to hash.
 *
 * @param   ctx                 The crypto context.
 * @param   data                Data to be hashed.
 * @param   sz                  Size of the data to hash.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_update(struct crypto_context *ctx,
		  const void *data,
		  unsigned sz)
{
	struct scatterlist sg;
	int rc = 0;

	if (!data || !sz || !ctx)
		return -EINVAL;

	sg_init_table(&sg, 1);
	sg_set_buf(&sg, data, sz);

	rc = crypto_hash_update(&ctx->desc, &sg, sg.length);
	if (rc) {
		logError("Failed on crypto_hash_update(). rc=%d.", -rc);
		return rc;
	}

	return 0;
}

/*************************************************************************/

/*
 * Adds an entire page to hash.
 *
 * @param   ctx                 The crypto context.
 * @param   page                Page to be hashed.
 * @param   sz                  Size of the data to hash.
 * @param   offset              An offset into the page.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_update_page(struct crypto_context *ctx,
		       struct page *page,
		       unsigned sz,
		       unsigned offset)
{
	struct scatterlist sl;
	int rc = 0;

	if (!page || !ctx)
		return -EINVAL;

	sg_init_table(&sl, 1);
	sg_set_page(&sl, page, sz, offset & ~PAGE_MASK);

	rc = crypto_hash_update(&ctx->desc, &sl, sl.length);
	if (rc) {
		logError("Failed on crypto_hash_update(). rc=%d.", -rc);
		return rc;
	}

	return rc;
}

/*************************************************************************/

/*
 * Returns the size of the hash that is generated by the chosen algorithm.
 *
 * @param   ctx                 The crypto context.
 * @param   out                 The size of the digest for this algorithm.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_get_digestsize(struct crypto_context *ctx,
			  int *out)
{
	if (!ctx)
		return -EINVAL;

	if (out)
		*out = crypto_hash_digestsize(ctx->desc.tfm);

	return out ? 0 : -EINVAL;
}

/*************************************************************************/

/*
 * This function completes the hashing process. If the hash pointer is
 * NULL, the funciton will deallocate memory but will not generate a hash.
 *
 * @param   ctx                 The crypto context.
 * @param   hash                A buffer that will receive the hash.
 * @param   hash_sz             Size of the hash buffer.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_end(struct crypto_context *ctx,
	       uint8_t *hash,
	       unsigned hash_sz)
{
	int rc = 0;

	if (!ctx)
		return -EINVAL;

	if (hash) {
		/* Check that the expected size of the digest is correct */
		unsigned digest_sz = crypto_hash_digestsize(ctx->desc.tfm);
		if (hash_sz < digest_sz) {
			logError("Output buffer size is too small.");
			return -EINVAL;
		}

		/* Hash the input buffer */
		rc = crypto_hash_final(&ctx->desc, hash);
		if (rc)
			logError("Failed on crypto_hash_digest(). rc=%d.", -rc);
	}

	crypto_free_hash(ctx->desc.tfm);

	return rc;
}

/*************************************************************************/

/*
 * This is a convenience function that wraps the crypto hash functionality
 * into one call.
 *
 * @param   alg                 The hashing algorithm to be used.
 * @param   data                Pointer to the data being hashed.
 * @param   sz                  Size of the data.
 * @param   hash                A buffer that will receive the hash.
 * @param   hash_sz             Size of the hash buffer.
 *
 * @return  0                   No Errors.
 *          -EINVAL             Invalid parameters.
 */
int crypto_once(unsigned alg,
		const void *data,
		unsigned sz,
		uint8_t *hash,
		unsigned hash_sz)
{
	struct crypto_context ctx = {};
	int rc = 0;

	rc = crypto_begin(alg, &ctx);
	if (rc) {
		logError("Failed on crypto_begin(). rc=%d.", -rc);
		return rc;
	}

	rc = crypto_update(&ctx, data, sz);
	if (rc) {
		logError("Failed on crypto_update(). rc=%d.", -rc);
		return rc;
	}

	rc = crypto_end(&ctx, hash, hash_sz);
	if (rc) {
		logError("Failed on crypto_end(). rc=%d.", -rc);
		return rc;
	}

	return 0;
}
