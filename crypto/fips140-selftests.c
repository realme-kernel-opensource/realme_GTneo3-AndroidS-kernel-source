// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
 *
 * Authors: Elena Petrova <lenaptr@google.com>,
 *          Eric Biggers <ebiggers@google.com>
 *
 * Self-tests of fips140.ko cryptographic functionality.  These are run at
 * module load time to fulfill FIPS 140 and NIAP FPT_TST_EXT.1 requirements.
 *
 * The actual requirements for these self-tests are somewhat vague, but
 * section 9 ("Self-Tests") of the FIPS 140-2 Implementation Guidance document
 * (https://csrc.nist.gov/csrc/media/projects/cryptographic-module-validation-program/documents/fips140-2/fips1402ig.pdf)
 * is somewhat helpful.  Basically, all implementations of all FIPS approved
 * algorithms (including modes of operation) must be tested.  However:
 *
 *   - There are provisions for skipping tests that are already sufficiently
 *     covered by other tests.  E.g., HMAC-SHA256 may cover SHA-256.
 *
 *   - Only one test vector is required per algorithm, and it can be generated
 *     by any known-good implementation or taken from any official document.
 *
 *   - For ciphers, both encryption and decryption must be tested.
 *
 *   - Only one key size per algorithm needs to be tested.
 *
 * There is some ambiguity about whether all implementations of each algorithm
 * must be tested, or whether it is sufficient to test just the highest priority
 * implementation.  To be safe we test all implementations, except ones that can
 * be excluded by one of the rules above.
 *
 * See fips140_selftests[] for the list of tests we've selected.  Currently, all
 * our test vectors except the AES-CBC-CTS and DRBG ones were generated by the
 * script tools/crypto/gen_fips140_testvecs.py, using the known-good
 * implementations in the Python packages hashlib, pycryptodome, and
 * cryptography.
 *
 * Note that we don't reuse the upstream crypto API's self-tests
 * (crypto/testmgr.{c,h}), for several reasons:
 *
 *   - To meet FIPS requirements, the self-tests must be located within the FIPS
 *     module boundary (fips140.ko).  But testmgr is integrated into the crypto
 *     API framework and can't be extracted into the module.
 *
 *   - testmgr is much more heavyweight than required for FIPS and NIAP; it
 *     tests more algorithms and does more tests per algorithm, as it's meant to
 *     do proper testing and not just meet certification requirements.  We need
 *     tests that can run with minimal overhead on every boot-up.
 *
 *   - Despite being more heavyweight in general, testmgr doesn't test the
 *     SHA-256 and AES library APIs, despite that being needed here.
 */
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/drbg.h>
#include <crypto/hash.h>
#include <crypto/rng.h>
#include <crypto/sha.h>
#include <crypto/skcipher.h>

#include "fips140-module.h"

/* Test vector for an AEAD algorithm */
struct aead_testvec {
	const u8 *key;
	size_t key_size;
	const u8 *iv;
	size_t iv_size;
	const u8 *assoc;
	size_t assoc_size;
	const u8 *plaintext;
	size_t plaintext_size;
	const u8 *ciphertext;
	size_t ciphertext_size;
};

/* Test vector for a length-preserving encryption algorithm */
struct skcipher_testvec {
	const u8 *key;
	size_t key_size;
	const u8 *iv;
	size_t iv_size;
	const u8 *plaintext;
	const u8 *ciphertext;
	size_t message_size;
};

/* Test vector for a hash algorithm */
struct hash_testvec {
	const u8 *key;
	size_t key_size;
	const u8 *message;
	size_t message_size;
	const u8 *digest;
	size_t digest_size;
};

/* Test vector for a DRBG algorithm */
struct drbg_testvec {
	const u8 *entropy;
	size_t entropy_size;
	const u8 *pers;
	size_t pers_size;
	const u8 *entpr_a;
	const u8 *entpr_b;
	size_t entpr_size;
	const u8 *add_a;
	const u8 *add_b;
	size_t add_size;
	const u8 *output;
	size_t out_size;
};

struct fips_test {
	/* The name of the algorithm, in crypto API syntax */
	const char *alg;

	/*
	 * The optional list of implementations to test.  @func will be called
	 * once per implementation, or once with @alg if this list is empty.
	 * The implementation names must be given in crypto API syntax, or in
	 * the case of a library implementation should have "-lib" appended.
	 */
	const char *impls[8];

	/*
	 * The test function.  It should execute a known-answer test on an
	 * algorithm implementation, using the below test vector.
	 */
	int __must_check (*func)(const struct fips_test *test,
				 const char *impl);

	/* The test vector, with a format specific to the type of algorithm */
	union {
		struct aead_testvec aead;
		struct skcipher_testvec skcipher;
		struct hash_testvec hash;
		struct drbg_testvec drbg;
	};
};

/* Maximum IV size (in bytes) among any algorithm tested here */
#define MAX_IV_SIZE	16

static int __init __must_check
fips_check_result(u8 *result, const u8 *expected_result, size_t result_size,
		  const char *impl, const char *operation)
{
#ifdef CONFIG_CRYPTO_FIPS140_MOD_ERROR_INJECTION
	/* Inject a failure (via corrupting the result) if requested. */
	if (fips140_broken_alg && strcmp(impl, fips140_broken_alg) == 0)
		result[0] ^= 0xff;
#endif
	if (memcmp(result, expected_result, result_size) != 0) {
		pr_err("wrong result from %s %s\n", impl, operation);
		return -EBADMSG;
	}
	return 0;
}

/*
 * None of the algorithms should be ASYNC, as the FIPS module doesn't register
 * any ASYNC algorithms.  (The ASYNC flag is only declared by hardware
 * algorithms, which would need their own FIPS certification.)
 *
 * Ideally we would verify alg->cra_module == THIS_MODULE here as well, but that
 * doesn't work because the files are compiled as built-in code.
 */
static int __init __must_check
fips_validate_alg(const struct crypto_alg *alg)
{
	if (alg->cra_flags & CRYPTO_ALG_ASYNC) {
		pr_err("unexpectedly got async implementation of %s (%s)\n",
		       alg->cra_name, alg->cra_driver_name);
		return -EINVAL;
	}
	return 0;
}

static int __init __must_check
fips_handle_alloc_tfm_error(const char *impl, int err)
{
	if (err == -ENOENT) {
		/*
		 * The requested implementation of the algorithm wasn't found.
		 * This is expected if the CPU lacks a feature the
		 * implementation needs, such as the ARMv8 Crypto Extensions.
		 *
		 * When this happens, the implementation isn't available for
		 * use, so we can't test it, nor do we need to.  So we just skip
		 * the test.
		 */
		pr_info("%s is unavailable (no CPU support?), skipping testing it\n",
			impl);
		return 0;
	}
	pr_err("failed to allocate %s tfm: %d\n", impl, err);
	return err;
}

static int __init __must_check
fips_test_aes_library(const struct fips_test *test, const char *impl)
{
	const struct skcipher_testvec *vec = &test->skcipher;
	struct crypto_aes_ctx ctx;
	u8 block[AES_BLOCK_SIZE];
	int err;

	if (WARN_ON(vec->message_size != AES_BLOCK_SIZE))
		return -EINVAL;

	err = aes_expandkey(&ctx, vec->key, vec->key_size);
	if (err) {
		pr_err("aes_expandkey() failed: %d\n", err);
		return err;
	}
	aes_encrypt(&ctx, block, vec->plaintext);
	err = fips_check_result(block, vec->ciphertext, AES_BLOCK_SIZE,
				impl, "encryption");
	if (err)
		return err;
	aes_decrypt(&ctx, block, block);
	return fips_check_result(block, vec->plaintext, AES_BLOCK_SIZE,
				 impl, "decryption");
}

/* Test a length-preserving symmetric cipher using the crypto_skcipher API. */
static int __init __must_check
fips_test_skcipher(const struct fips_test *test, const char *impl)
{
	const struct skcipher_testvec *vec = &test->skcipher;
	struct crypto_skcipher *tfm;
	struct skcipher_request *req = NULL;
	u8 *message = NULL;
	struct scatterlist sg;
	u8 iv[MAX_IV_SIZE];
	int err;

	if (WARN_ON(vec->iv_size > MAX_IV_SIZE))
		return -EINVAL;
	if (WARN_ON(vec->message_size <= 0))
		return -EINVAL;

	tfm = crypto_alloc_skcipher(impl, 0, 0);
	if (IS_ERR(tfm))
		return fips_handle_alloc_tfm_error(impl, PTR_ERR(tfm));
	err = fips_validate_alg(&crypto_skcipher_alg(tfm)->base);
	if (err)
		goto out;
	if (crypto_skcipher_ivsize(tfm) != vec->iv_size) {
		pr_err("%s has wrong IV size\n", impl);
		err = -EINVAL;
		goto out;
	}

	req = skcipher_request_alloc(tfm, GFP_KERNEL);
	message = kmemdup(vec->plaintext, vec->message_size, GFP_KERNEL);
	if (!req || !message) {
		err = -ENOMEM;
		goto out;
	}
	sg_init_one(&sg, message, vec->message_size);

	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP,
				      NULL, NULL);
	skcipher_request_set_crypt(req, &sg, &sg, vec->message_size, iv);

	err = crypto_skcipher_setkey(tfm, vec->key, vec->key_size);
	if (err) {
		pr_err("failed to set %s key: %d\n", impl, err);
		goto out;
	}

	/* Encrypt the plaintext, then verify the resulting ciphertext. */
	memcpy(iv, vec->iv, vec->iv_size);
	err = crypto_skcipher_encrypt(req);
	if (err) {
		pr_err("%s encryption failed: %d\n", impl, err);
		goto out;
	}
	err = fips_check_result(message, vec->ciphertext, vec->message_size,
				impl, "encryption");
	if (err)
		goto out;

	/* Decrypt the ciphertext, then verify the resulting plaintext. */
	memcpy(iv, vec->iv, vec->iv_size);
	err = crypto_skcipher_decrypt(req);
	if (err) {
		pr_err("%s decryption failed: %d\n", impl, err);
		goto out;
	}
	err = fips_check_result(message, vec->plaintext, vec->message_size,
				impl, "decryption");
out:
	kfree(message);
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return err;
}

/* Test an AEAD using the crypto_aead API. */
static int __init __must_check
fips_test_aead(const struct fips_test *test, const char *impl)
{
	const struct aead_testvec *vec = &test->aead;
	const int tag_size = vec->ciphertext_size - vec->plaintext_size;
	struct crypto_aead *tfm;
	struct aead_request *req = NULL;
	u8 *assoc = NULL;
	u8 *message = NULL;
	struct scatterlist sg[2];
	int sg_idx = 0;
	u8 iv[MAX_IV_SIZE];
	int err;

	if (WARN_ON(vec->iv_size > MAX_IV_SIZE))
		return -EINVAL;
	if (WARN_ON(vec->ciphertext_size <= vec->plaintext_size))
		return -EINVAL;

	tfm = crypto_alloc_aead(impl, 0, 0);
	if (IS_ERR(tfm))
		return fips_handle_alloc_tfm_error(impl, PTR_ERR(tfm));
	err = fips_validate_alg(&crypto_aead_alg(tfm)->base);
	if (err)
		goto out;
	if (crypto_aead_ivsize(tfm) != vec->iv_size) {
		pr_err("%s has wrong IV size\n", impl);
		err = -EINVAL;
		goto out;
	}

	req = aead_request_alloc(tfm, GFP_KERNEL);
	assoc = kmemdup(vec->assoc, vec->assoc_size, GFP_KERNEL);
	message = kzalloc(vec->ciphertext_size, GFP_KERNEL);
	if (!req || !assoc || !message) {
		err = -ENOMEM;
		goto out;
	}
	memcpy(message, vec->plaintext, vec->plaintext_size);

	sg_init_table(sg, ARRAY_SIZE(sg));
	if (vec->assoc_size)
		sg_set_buf(&sg[sg_idx++], assoc, vec->assoc_size);
	sg_set_buf(&sg[sg_idx++], message, vec->ciphertext_size);

	aead_request_set_ad(req, vec->assoc_size);
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP, NULL, NULL);

	err = crypto_aead_setkey(tfm, vec->key, vec->key_size);
	if (err) {
		pr_err("failed to set %s key: %d\n", impl, err);
		goto out;
	}

	err = crypto_aead_setauthsize(tfm, tag_size);
	if (err) {
		pr_err("failed to set %s authentication tag size: %d\n",
		       impl, err);
		goto out;
	}

	/*
	 * Encrypt the plaintext, then verify the resulting ciphertext (which
	 * includes the authentication tag).
	 */
	memcpy(iv, vec->iv, vec->iv_size);
	aead_request_set_crypt(req, sg, sg, vec->plaintext_size, iv);
	err = crypto_aead_encrypt(req);
	if (err) {
		pr_err("%s encryption failed: %d\n", impl, err);
		goto out;
	}
	err = fips_check_result(message, vec->ciphertext, vec->ciphertext_size,
				impl, "encryption");
	if (err)
		goto out;

	/*
	 * Decrypt the ciphertext (which includes the authentication tag), then
	 * verify the resulting plaintext.
	 */
	memcpy(iv, vec->iv, vec->iv_size);
	aead_request_set_crypt(req, sg, sg, vec->ciphertext_size, iv);
	err = crypto_aead_decrypt(req);
	if (err) {
		pr_err("%s decryption failed: %d\n", impl, err);
		goto out;
	}
	err = fips_check_result(message, vec->plaintext, vec->plaintext_size,
				impl, "decryption");
out:
	kfree(message);
	kfree(assoc);
	aead_request_free(req);
	crypto_free_aead(tfm);
	return err;
}

/*
 * Test a hash algorithm using the crypto_shash API.
 *
 * Note that we don't need to test the crypto_ahash API too, since none of the
 * hash algorithms in the FIPS module have the ASYNC flag, and thus there will
 * be no hash algorithms that can be accessed only through crypto_ahash.
 */
static int __init __must_check
fips_test_hash(const struct fips_test *test, const char *impl)
{
	const struct hash_testvec *vec = &test->hash;
	struct crypto_shash *tfm;
	u8 digest[HASH_MAX_DIGESTSIZE];
	int err;

	if (WARN_ON(vec->digest_size > HASH_MAX_DIGESTSIZE))
		return -EINVAL;

	tfm = crypto_alloc_shash(impl, 0, 0);
	if (IS_ERR(tfm))
		return fips_handle_alloc_tfm_error(impl, PTR_ERR(tfm));
	err = fips_validate_alg(&crypto_shash_alg(tfm)->base);
	if (err)
		goto out;
	if (crypto_shash_digestsize(tfm) != vec->digest_size) {
		pr_err("%s has wrong digest size\n", impl);
		err = -EINVAL;
		goto out;
	}

	if (vec->key) {
		err = crypto_shash_setkey(tfm, vec->key, vec->key_size);
		if (err) {
			pr_err("failed to set %s key: %d\n", impl, err);
			goto out;
		}
	}

	err = crypto_shash_tfm_digest(tfm, vec->message, vec->message_size,
				      digest);
	if (err) {
		pr_err("%s digest computation failed: %d\n", impl, err);
		goto out;
	}
	err = fips_check_result(digest, vec->digest, vec->digest_size,
				impl, "digest");
out:
	crypto_free_shash(tfm);
	return err;
}

static int __init __must_check
fips_test_sha256_library(const struct fips_test *test, const char *impl)
{
	const struct hash_testvec *vec = &test->hash;
	u8 digest[SHA256_DIGEST_SIZE];

	if (WARN_ON(vec->digest_size != SHA256_DIGEST_SIZE))
		return -EINVAL;

	sha256(vec->message, vec->message_size, digest);
	return fips_check_result(digest, vec->digest, vec->digest_size,
				 impl, "digest");
}

/* Test a DRBG using the crypto_rng API. */
static int __init __must_check
fips_test_drbg(const struct fips_test *test, const char *impl)
{
	const struct drbg_testvec *vec = &test->drbg;
	struct crypto_rng *rng;
	u8 *output = NULL;
	struct drbg_test_data test_data;
	struct drbg_string addtl, pers, testentropy;
	int err;

	rng = crypto_alloc_rng(impl, 0, 0);
	if (IS_ERR(rng))
		return fips_handle_alloc_tfm_error(impl, PTR_ERR(rng));
	err = fips_validate_alg(&crypto_rng_alg(rng)->base);
	if (err)
		goto out;

	output = kzalloc(vec->out_size, GFP_KERNEL);
	if (!output) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Initialize the DRBG with the entropy and personalization string given
	 * in the test vector.
	 */
	test_data.testentropy = &testentropy;
	drbg_string_fill(&testentropy, vec->entropy, vec->entropy_size);
	drbg_string_fill(&pers, vec->pers, vec->pers_size);
	err = crypto_drbg_reset_test(rng, &pers, &test_data);
	if (err) {
		pr_err("failed to reset %s\n", impl);
		goto out;
	}

	/*
	 * Generate some random bytes using the additional data string provided
	 * in the test vector.  Also use the additional entropy if provided
	 * (relevant for the prediction-resistant DRBG variants only).
	 */
	drbg_string_fill(&addtl, vec->add_a, vec->add_size);
	if (vec->entpr_size) {
		drbg_string_fill(&testentropy, vec->entpr_a, vec->entpr_size);
		err = crypto_drbg_get_bytes_addtl_test(rng, output,
						       vec->out_size, &addtl,
						       &test_data);
	} else {
		err = crypto_drbg_get_bytes_addtl(rng, output, vec->out_size,
						  &addtl);
	}
	if (err) {
		pr_err("failed to get bytes from %s (try 1): %d\n",
		       impl, err);
		goto out;
	}

	/*
	 * Do the same again, using a second additional data string, and (when
	 * applicable) a second additional entropy string.
	 */
	drbg_string_fill(&addtl, vec->add_b, vec->add_size);
	if (test->drbg.entpr_size) {
		drbg_string_fill(&testentropy, vec->entpr_b, vec->entpr_size);
		err = crypto_drbg_get_bytes_addtl_test(rng, output,
						       vec->out_size, &addtl,
						       &test_data);
	} else {
		err = crypto_drbg_get_bytes_addtl(rng, output, vec->out_size,
						  &addtl);
	}
	if (err) {
		pr_err("failed to get bytes from %s (try 2): %d\n",
		       impl, err);
		goto out;
	}

	/* Check that the DRBG generated the expected output. */
	err = fips_check_result(output, vec->output, vec->out_size,
				impl, "get_bytes");
out:
	kfree(output);
	crypto_free_rng(rng);
	return err;
}

/* Include the test vectors generated by the Python script. */
#include "fips140-generated-testvecs.h"

/*
 * List of all self-tests.  Keep this in sync with fips140_algorithms[].
 *
 * When possible, we have followed the FIPS 140-2 Implementation Guidance (IG)
 * document when creating this list of tests.  The result is intended to be a
 * list of tests that is near-minimal (and thus minimizes runtime overhead)
 * while complying with all requirements.  For additional details, see the
 * comment at the beginning of this file.
 */
static const struct fips_test fips140_selftests[] __initconst = {
	/*
	 * Test for the AES library API.
	 *
	 * Since the AES library API may use its own AES implementation and the
	 * module provides no support for composing it with a mode of operation
	 * (it's just plain AES), we must test it directly.
	 *
	 * In contrast, we don't need to directly test the "aes" ciphers that
	 * are accessible through the crypto_cipher API (e.g. "aes-ce"), as they
	 * are covered indirectly by AES-CMAC and AES-ECB tests.
	 */
	{
		.alg		= "aes",
		.impls		= {"aes-lib"},
		.func		= fips_test_aes_library,
		.skcipher	= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.plaintext	= fips_message,
			.ciphertext	= fips_aes_ecb_ciphertext,
			.message_size	= 16,
		}
	},
	/*
	 * Tests for AES-CMAC, a.k.a. "cmac(aes)" in crypto API syntax.
	 *
	 * The IG requires that each underlying AES implementation be tested in
	 * an authenticated mode, if implemented.  Of such modes, this module
	 * implements AES-GCM and AES-CMAC.  However, AES-GCM doesn't "count"
	 * because this module's implementations of AES-GCM won't actually be
	 * FIPS-approved, due to a quirk in the FIPS requirements.
	 *
	 * Therefore, for us this requirement applies to AES-CMAC, so we must
	 * test the "cmac" template composed with each "aes" implementation.
	 *
	 * Separately from the above, we also must test all standalone
	 * implementations of "cmac(aes)" such as "cmac-aes-ce", as they don't
	 * reuse another full AES implementation and thus can't be covered by
	 * another test.
	 */
	{
		.alg		= "cmac(aes)",
		.impls		= {
			/* "cmac" template with all "aes" implementations */
			"cmac(aes-generic)",
			"cmac(aes-arm64)",
			"cmac(aes-ce)",
			/* All standalone implementations of "cmac(aes)" */
			"cmac-aes-neon",
			"cmac-aes-ce",
		},
		.func		= fips_test_hash,
		.hash		= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_aes_cmac_digest,
			.digest_size	= sizeof(fips_aes_cmac_digest),
		}
	},
	/*
	 * Tests for AES-ECB, a.k.a. "ecb(aes)" in crypto API syntax.
	 *
	 * The IG requires that each underlying AES implementation be tested in
	 * a mode that exercises the encryption direction of AES and in a mode
	 * that exercises the decryption direction of AES.  CMAC only covers the
	 * encryption direction, so we choose ECB to test decryption.  Thus, we
	 * test the "ecb" template composed with each "aes" implementation.
	 *
	 * Separately from the above, we also must test all standalone
	 * implementations of "ecb(aes)" such as "ecb-aes-ce", as they don't
	 * reuse another full AES implementation and thus can't be covered by
	 * another test.
	 */
	{
		.alg		= "ecb(aes)",
		.impls		= {
			/* "ecb" template with all "aes" implementations */
			"ecb(aes-generic)",
			"ecb(aes-arm64)",
			"ecb(aes-ce)",
			/* All standalone implementations of "ecb(aes)" */
			"ecb-aes-neon",
			"ecb-aes-neonbs",
			"ecb-aes-ce",
		},
		.func		= fips_test_skcipher,
		.skcipher	= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.plaintext	= fips_message,
			.ciphertext	= fips_aes_ecb_ciphertext,
			.message_size	= sizeof(fips_message)
		}
	},
	/*
	 * Tests for AES-CBC, AES-CBC-CTS, AES-CTR, AES-XTS, and AES-GCM.
	 *
	 * According to the IG, an AES mode of operation doesn't need to have
	 * its own test, provided that (a) both the encryption and decryption
	 * directions of the underlying AES implementation are already tested
	 * via other mode(s), and (b) in the case of an authenticated mode, at
	 * least one other authenticated mode is already tested.  The tests of
	 * the "cmac" and "ecb" templates fulfill these conditions; therefore,
	 * we don't need to test any other AES mode templates.
	 *
	 * This does *not* apply to standalone implementations of these modes
	 * such as "cbc-aes-ce", as such implementations don't reuse another
	 * full AES implementation and thus can't be covered by another test.
	 * We must test all such standalone implementations.
	 *
	 * The AES-GCM test isn't actually required, as it's expected that this
	 * module's AES-GCM implementation won't actually be able to be
	 * FIPS-approved.  This is unfortunate; it's caused by the FIPS
	 * requirements for GCM being incompatible with GCM implementations that
	 * don't generate their own IVs.  We choose to still include the AES-GCM
	 * test to keep it on par with the other FIPS-approved algorithms, in
	 * case it turns out that AES-GCM can be approved after all.
	 */
	{
		.alg		= "cbc(aes)",
		.impls		= {
			/* All standalone implementations of "cbc(aes)" */
			"cbc-aes-neon",
			"cbc-aes-neonbs",
			"cbc-aes-ce",
		},
		.func		= fips_test_skcipher,
		.skcipher	= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.iv		= fips_aes_iv,
			.iv_size	= sizeof(fips_aes_iv),
			.plaintext	= fips_message,
			.ciphertext	= fips_aes_cbc_ciphertext,
			.message_size	= sizeof(fips_message),
		}
	}, {
		.alg		= "cts(cbc(aes))",
		.impls		= {
			/* All standalone implementations of "cts(cbc(aes))" */
			"cts-cbc-aes-neon",
			"cts-cbc-aes-ce",
		},
		.func		= fips_test_skcipher,
		/* Test vector taken from RFC 3962 */
		.skcipher	= {
			.key    = "\x63\x68\x69\x63\x6b\x65\x6e\x20"
				  "\x74\x65\x72\x69\x79\x61\x6b\x69",
			.key_size = 16,
			.iv	= "\x00\x00\x00\x00\x00\x00\x00\x00"
				  "\x00\x00\x00\x00\x00\x00\x00\x00",
			.iv_size = 16,
			.plaintext = "\x49\x20\x77\x6f\x75\x6c\x64\x20"
				     "\x6c\x69\x6b\x65\x20\x74\x68\x65"
				     "\x20\x47\x65\x6e\x65\x72\x61\x6c"
				     "\x20\x47\x61\x75\x27\x73\x20",
			.ciphertext = "\xfc\x00\x78\x3e\x0e\xfd\xb2\xc1"
				      "\xd4\x45\xd4\xc8\xef\xf7\xed\x22"
				      "\x97\x68\x72\x68\xd6\xec\xcc\xc0"
				      "\xc0\x7b\x25\xe2\x5e\xcf\xe5",
			.message_size = 31,
		}
	}, {
		.alg		= "ctr(aes)",
		.impls		= {
			/* All standalone implementations of "ctr(aes)" */
			"ctr-aes-neon",
			"ctr-aes-neonbs",
			"ctr-aes-ce",
		},
		.func		= fips_test_skcipher,
		.skcipher	= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.iv		= fips_aes_iv,
			.iv_size	= sizeof(fips_aes_iv),
			.plaintext	= fips_message,
			.ciphertext	= fips_aes_ctr_ciphertext,
			.message_size	= sizeof(fips_message),
		}
	}, {
		.alg		= "xts(aes)",
		.impls		= {
			/* All standalone implementations of "xts(aes)" */
			"xts-aes-neon",
			"xts-aes-neonbs",
			"xts-aes-ce",
		},
		.func		= fips_test_skcipher,
		.skcipher	= {
			.key		= fips_aes_xts_key,
			.key_size	= sizeof(fips_aes_xts_key),
			.iv		= fips_aes_iv,
			.iv_size	= sizeof(fips_aes_iv),
			.plaintext	= fips_message,
			.ciphertext	= fips_aes_xts_ciphertext,
			.message_size	= sizeof(fips_message),
		}
	}, {
		.alg		= "gcm(aes)",
		.impls		= {
			/* All standalone implementations of "gcm(aes)" */
			"gcm-aes-ce",
		},
		.func		= fips_test_aead,
		.aead		= {
			.key		= fips_aes_key,
			.key_size	= sizeof(fips_aes_key),
			.iv		= fips_aes_iv,
			/* The GCM implementations assume an IV size of 12. */
			.iv_size	= 12,
			.assoc		= fips_aes_gcm_assoc,
			.assoc_size	= sizeof(fips_aes_gcm_assoc),
			.plaintext	= fips_message,
			.plaintext_size	= sizeof(fips_message),
			.ciphertext	= fips_aes_gcm_ciphertext,
			.ciphertext_size = sizeof(fips_aes_gcm_ciphertext),
		}
	},

	/* Tests for SHA-1 */
	{
		.alg		= "sha1",
		.impls		= {
			/* All implementations of "sha1" */
			"sha1-generic",
			"sha1-ce"
		},
		.func		= fips_test_hash,
		.hash		= {
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_sha1_digest,
			.digest_size	= sizeof(fips_sha1_digest)
		}
	},
	/*
	 * Tests for all SHA-256 implementations other than the sha256() library
	 * function.  As per the IG, these tests also fulfill the tests for the
	 * corresponding SHA-224 implementations.
	 */
	{
		.alg		= "sha256",
		.impls		= {
			/* All implementations of "sha256" */
			"sha256-generic",
			"sha256-arm64",
			"sha256-ce",
		},
		.func		= fips_test_hash,
		.hash		= {
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_sha256_digest,
			.digest_size	= sizeof(fips_sha256_digest)
		}
	},
	/*
	 * Test for the sha256() library function.  This must be tested
	 * separately because it may use its own SHA-256 implementation.
	 */
	{
		.alg		= "sha256",
		.impls		= {"sha256-lib"},
		.func		= fips_test_sha256_library,
		.hash		= {
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_sha256_digest,
			.digest_size	= sizeof(fips_sha256_digest)
		}
	},
	/*
	 * Tests for all SHA-512 implementations.  As per the IG, these tests
	 * also fulfill the tests for the corresponding SHA-384 implementations.
	 */
	{
		.alg		= "sha512",
		.impls		= {
			/* All implementations of "sha512" */
			"sha512-generic",
			"sha512-arm64",
			"sha512-ce",
		},
		.func		= fips_test_hash,
		.hash		= {
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_sha512_digest,
			.digest_size	= sizeof(fips_sha512_digest)
		}
	},
	/*
	 * Test for HMAC.  As per the IG, only one HMAC test is required,
	 * provided that the same HMAC code is shared by all HMAC-SHA*.  This is
	 * true in our case.  We choose HMAC-SHA256 for the test.
	 *
	 * Note that as per the IG, this can fulfill the test for the underlying
	 * SHA.  However, we don't currently rely on this.
	 */
	{
		.alg		= "hmac(sha256)",
		.func		= fips_test_hash,
		.hash		= {
			.key		= fips_hmac_key,
			.key_size	= sizeof(fips_hmac_key),
			.message	= fips_message,
			.message_size	= sizeof(fips_message),
			.digest		= fips_hmac_sha256_digest,
			.digest_size	= sizeof(fips_hmac_sha256_digest)
		}
	},
	/*
	 * Known-answer tests for the SP800-90A DRBG algorithms.
	 *
	 * These test vectors were manually extracted from
	 * https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/drbg/drbgtestvectors.zip.
	 *
	 * The selection of these tests follows the FIPS 140-2 IG as well as
	 * Section 11 of SP800-90A:
	 *
	 * - We must test all DRBG types (HMAC, Hash, and CTR) that the module
	 *   implements.  However, currently the module only implements
	 *   HMAC_DRBG (since CONFIG_CRYPTO_DRBG_CTR and CONFIG_CRYPTO_DRBG_HASH
	 *   aren't enabled).  Therefore, we only need to test HMAC_DRBG.
	 *
	 * - We only need to test one HMAC variant.
	 *
	 * - We must test all DRBG operations: Instantiate(), Reseed(), and
	 *   Generate().  However, a single test sequence with a single output
	 *   comparison may cover all three operations, and this is what we do.
	 *   Note that Reseed() happens implicitly via the use of the additional
	 *   input and also via the use of prediction resistance when enabled.
	 *
	 * - The personalization string, additional input, and prediction
	 *   resistance support must be tested.  Therefore we have chosen test
	 *   vectors that have a nonempty personalization string and nonempty
	 *   additional input, and we test the prediction-resistant variant.
	 *   Testing the non-prediction-resistant variant is not required.
	 */
	{
		.alg	= "drbg_pr_hmac_sha256",
		.func	= fips_test_drbg,
		.drbg	= {
			.entropy =
				"\xc7\xcc\xbc\x67\x7e\x21\x66\x1e\x27\x2b\x63\xdd"
				"\x3a\x78\xdc\xdf\x66\x6d\x3f\x24\xae\xcf\x37\x01"
				"\xa9\x0d\x89\x8a\xa7\xdc\x81\x58\xae\xb2\x10\x15"
				"\x7e\x18\x44\x6d\x13\xea\xdf\x37\x85\xfe\x81\xfb",
			.entropy_size = 48,
			.entpr_a =
				"\x7b\xa1\x91\x5b\x3c\x04\xc4\x1b\x1d\x19\x2f\x1a"
				"\x18\x81\x60\x3c\x6c\x62\x91\xb7\xe9\xf5\xcb\x96"
				"\xbb\x81\x6a\xcc\xb5\xae\x55\xb6",
			.entpr_b =
				"\x99\x2c\xc7\x78\x7e\x3b\x88\x12\xef\xbe\xd3\xd2"
				"\x7d\x2a\xa5\x86\xda\x8d\x58\x73\x4a\x0a\xb2\x2e"
				"\xbb\x4c\x7e\xe3\x9a\xb6\x81\xc1",
			.entpr_size = 32,
			.output =
				"\x95\x6f\x95\xfc\x3b\xb7\xfe\x3e\xd0\x4e\x1a\x14"
				"\x6c\x34\x7f\x7b\x1d\x0d\x63\x5e\x48\x9c\x69\xe6"
				"\x46\x07\xd2\x87\xf3\x86\x52\x3d\x98\x27\x5e\xd7"
				"\x54\xe7\x75\x50\x4f\xfb\x4d\xfd\xac\x2f\x4b\x77"
				"\xcf\x9e\x8e\xcc\x16\xa2\x24\xcd\x53\xde\x3e\xc5"
				"\x55\x5d\xd5\x26\x3f\x89\xdf\xca\x8b\x4e\x1e\xb6"
				"\x88\x78\x63\x5c\xa2\x63\x98\x4e\x6f\x25\x59\xb1"
				"\x5f\x2b\x23\xb0\x4b\xa5\x18\x5d\xc2\x15\x74\x40"
				"\x59\x4c\xb4\x1e\xcf\x9a\x36\xfd\x43\xe2\x03\xb8"
				"\x59\x91\x30\x89\x2a\xc8\x5a\x43\x23\x7c\x73\x72"
				"\xda\x3f\xad\x2b\xba\x00\x6b\xd1",
			.out_size = 128,
			.add_a =
				"\x18\xe8\x17\xff\xef\x39\xc7\x41\x5c\x73\x03\x03"
				"\xf6\x3d\xe8\x5f\xc8\xab\xe4\xab\x0f\xad\xe8\xd6"
				"\x86\x88\x55\x28\xc1\x69\xdd\x76",
			.add_b =
				"\xac\x07\xfc\xbe\x87\x0e\xd3\xea\x1f\x7e\xb8\xe7"
				"\x9d\xec\xe8\xe7\xbc\xf3\x18\x25\x77\x35\x4a\xaa"
				"\x00\x99\x2a\xdd\x0a\x00\x50\x82",
			.add_size = 32,
			.pers =
				"\xbc\x55\xab\x3c\xf6\x52\xb0\x11\x3d\x7b\x90\xb8"
				"\x24\xc9\x26\x4e\x5a\x1e\x77\x0d\x3d\x58\x4a\xda"
				"\xd1\x81\xe9\xf8\xeb\x30\x8f\x6f",
			.pers_size = 32,
		}
	}
};

static int __init __must_check
fips_run_test(const struct fips_test *test)
{
	int i;
	int err;

	/*
	 * If no implementations were specified, then just test the default one.
	 * Otherwise, test the specified list of implementations.
	 */

	if (test->impls[0] == NULL) {
		err = test->func(test, test->alg);
		if (err)
			pr_emerg("self-tests failed for algorithm %s: %d\n",
				 test->alg, err);
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(test->impls) && test->impls[i] != NULL;
	     i++) {
		err = test->func(test, test->impls[i]);
		if (err) {
			pr_emerg("self-tests failed for algorithm %s, implementation %s: %d\n",
				 test->alg, test->impls[i], err);
			return err;
		}
	}
	return 0;
}

bool __init fips140_run_selftests(void)
{
	int i;

	pr_info("running self-tests\n");
	for (i = 0; i < ARRAY_SIZE(fips140_selftests); i++) {
		if (fips_run_test(&fips140_selftests[i]) != 0) {
			/* The caller is responsible for calling panic(). */
			return false;
		}
	}
	pr_info("all self-tests passed\n");
	return true;
}
