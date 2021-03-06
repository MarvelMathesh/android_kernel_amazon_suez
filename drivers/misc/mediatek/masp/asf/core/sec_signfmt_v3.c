/*
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include "sec_sign_extension.h"
#include "sec_signfmt_v3.h"
#include "sec_signfmt_util.h"
#include "sec_log.h"
#include "sec_error.h"
#include "sec_boot_lib.h"
#include "sec_wrapper.h"
#include "sec_mtd_util.h"
#include "sec_osal.h"

/**************************************************************************
 *  MODULE NAME
 **************************************************************************/
#define MOD                         "SFMT_V3"


/******************************************************************************
 *  IMAGE VERIFICATION MEMORY DUMP FUNCTIONS
 ******************************************************************************/
#define DUMP_MORE_FOR_DEBUG 0

#if DUMP_MORE_FOR_DEBUG
static void sec_signfmt_dump_buffer(unsigned char *buf, unsigned int len)
{
	unsigned int i = 0;

	for (i = 1; i < len + 1; i++) {
		SMSG(true, "0x%x,", buf[i - 1]);

		if (0 == (i % 8))
			SMSG(true, "\n");
	}

	if (0 != (len % 8))
		SMSG(true, "\n");
}
#endif

/**************************************************************************
 *  FUNCTION To Generate Hash by Chunk
 **************************************************************************/
static int sec_signfmt_image_read(ASF_FILE fp, char *part_name, unsigned int seek_offset, char *read_buf,
				  unsigned int read_len)
{
	unsigned int read_sz = 0;
	unsigned int ret = SEC_OK;

#if DUMP_MORE_FOR_DEBUG
	SMSG(true, "[%s] Read image for length %d at offset 0x%x\n", MOD, read_len, seek_offset);
#endif

	/* read from file */
	if (ASF_FILE_NULL != fp) {
		ASF_SEEK_SET(fp, seek_offset * sizeof(char));
		read_sz = ASF_READ(fp, read_buf, read_len);

		return read_sz;
	}
	/* read from mtd */
	else {
		ret = sec_dev_read_image(pl2part(part_name), (char *)read_buf,
						seek_offset, read_len, NORMAL_ROM);
		if (SEC_OK != ret) {
			SMSG(true,
			     "[%s] read mtd '%s' fail at image offset 0x%x with length 0x%x\n", MOD,
			     (char *)pl2part(part_name), seek_offset, read_len);
			return 0;
		}

		return read_len;
	}
}


static int sec_signfmt_gen_hash_by_chunk(ASF_FILE img_fd, char *part_name, unsigned int img_hash_off,
					 unsigned int img_hash_len, unsigned char *final_hash_buf,
					 SEC_CRYPTO_HASH_TYPE hash_type, unsigned int chunk_size)
{
	unsigned int br = 0;
	unsigned int ret = 0;
	unsigned char *chunk_buf = NULL;
	unsigned char *hash_tmp = NULL;
	unsigned char *hash_comb = NULL;
	unsigned int seek_pos = 0;
	unsigned int hash_size = get_hash_size(hash_type);
#if DUMP_MORE_FOR_DEBUG
	unsigned int chunk_count = ((img_hash_len - 1) / chunk_size) + 1;
#endif
	unsigned int read_size = 0;
	unsigned int left_size = 0;

	if (!img_hash_len) {

		SMSG(true, "[%s] hash length is zero, no need to do hash\n", MOD);
		ret = -1;
		memset(final_hash_buf, 0x00, hash_size);
		goto end_error;
	}
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Hash size is %d (0x%x)\n", MOD, hash_size, hash_size);
	SMSG(sec_info.bMsg, "[%s] Offset is %d (0x%x)\n", MOD, img_hash_off, img_hash_off);
	SMSG(sec_info.bMsg, "[%s] Size is %d (0x%x)\n", MOD, img_hash_len, img_hash_len);
	SMSG(sec_info.bMsg, "[%s] Chunk size is %d (0x%x)\n", MOD, chunk_size, chunk_size);
	SMSG(sec_info.bMsg, "[%s] Chunk count is %d (0x%x)\n", MOD, chunk_count, chunk_count);
#endif

	/* allocate hash buffer */
	hash_tmp = ASF_MALLOC(hash_size);
	hash_comb = ASF_MALLOC(hash_size * 2);
	memset(hash_tmp, 0x00, hash_size);
	memset(hash_comb, 0x00, hash_size * 2);

	/* allocate buffer with known chunk size */
	chunk_buf = ASF_MALLOC(chunk_size);

	/* caculate first hash */
	seek_pos = img_hash_off;
	left_size = img_hash_len;
	read_size = (left_size >= chunk_size) ? chunk_size : left_size;
	br = sec_signfmt_image_read(img_fd, part_name, seek_pos * sizeof(char), (char *)chunk_buf,
				    read_size);
	if (br != read_size) {
		SMSG(true, "[%s] read image content fail, read offset = '0x%x'\n", MOD, seek_pos);
		ret = -2;
		goto end_error;
	}
	if (sec_hash(chunk_buf, read_size, hash_tmp, hash_size) == -1) {
		SMSG(true, "[%s] hash fail, offset is '0x%x'(A)\n", MOD, seek_pos);
		ret = -3;
		goto end_error;
	}
#if DUMP_MORE_FOR_DEBUG
	/* ------------------------------------- */
	/* dump hash value for debug             */
	/* ------------------------------------- */
	SMSG(sec_info.bMsg, "[%s] Data value(4 bytes) ==>\n", MOD);
	sec_signfmt_dump_buffer(chunk_buf, 4);

	SMSG(sec_info.bMsg, "[%s] Hash value(single) (0x%x):\n", MOD, seek_pos);
	sec_signfmt_dump_buffer(hash_tmp, hash_size);
#endif

	/* copy to compose buffer (first block) */
	mcpy(hash_comb, hash_tmp, hash_size);

	/* move next */
	seek_pos += read_size;
	left_size -= read_size;

	/* loop hash */
	while (left_size) {
		/* load data */
		read_size = (left_size >= chunk_size) ? chunk_size : left_size;
		br = sec_signfmt_image_read(img_fd, part_name, seek_pos * sizeof(char),
					    (char *)chunk_buf, read_size);

		if (br != read_size) {
			SMSG(true, "[%s] read image content fail, read offset = '0x%x'\n", MOD,
			     seek_pos);
			ret = -4;
			goto end_error;
		}

		/* caculate this hash */
		if (sec_hash(chunk_buf, read_size, hash_tmp, hash_size) == -1) {
			SMSG(true, "[%s] hash fail, offset is '0x%x'(B)\n", MOD, seek_pos);
			ret = -5;
			goto end_error;
		}
#if DUMP_MORE_FOR_DEBUG
		/* ------------------------------------- */
		/* dump hash value for debug             */
		/* ------------------------------------- */
		SMSG(sec_info.bMsg, "[%s] Data value(4 bytes) ==>\n", MOD);
		sec_signfmt_dump_buffer(chunk_buf, 4);

		SMSG(sec_info.bMsg, "[%s] Hash value(single) (0x%x):\n", MOD, seek_pos);
		sec_signfmt_dump_buffer(hash_tmp, hash_size);
#endif

		/* compose two hash to buffer (second block) */
		mcpy(hash_comb + hash_size, hash_tmp, hash_size);

		/* caculate compose hash */
		if (sec_hash(hash_comb, hash_size * 2, hash_tmp, hash_size) == -1) {
			SMSG(true, "[%s] hash fail, offset is '0x%x'(C)\n", MOD, seek_pos);
			ret = -6;
			goto end_error;
		}
#if DUMP_MORE_FOR_DEBUG
		/* ------------------------------------- */
		/* dump hash value for debug             */
		/* ------------------------------------- */
		SMSG(sec_info.bMsg, "[%s] Data value(4 bytes) ==>\n", MOD);
		sec_signfmt_dump_buffer(chunk_buf, 4);

		SMSG(sec_info.bMsg, "[%s] Hash value(comp) (0x%x):\n", MOD, seek_pos);
		sec_signfmt_dump_buffer(hash_tmp, hash_size);
#endif

		/* save this hash to compose buffer (first block) */
		mcpy(hash_comb, hash_tmp, hash_size);

		/* move next */
		seek_pos += read_size;
		left_size -= read_size;
	}

	/* ------------------------------------- */
	/* dump hash value for debug             */
	/* ------------------------------------- */
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Hash value(final) (0x%x):\n", MOD, seek_pos);
	sec_signfmt_dump_buffer(hash_tmp, hash_size);
#endif

	/* copy hash */
	mcpy(final_hash_buf, hash_tmp, hash_size);

end_error:
	ASF_FREE(hash_comb);
	ASF_FREE(chunk_buf);
	ASF_FREE(hash_tmp);

	return ret;
}


/**************************************************************************
 *  FUNCTION To Search Extension Header
 **************************************************************************/
static unsigned int sec_signfmt_search_extension(unsigned char *ext, unsigned int ext_len,
					   SEC_IMG_EXTENSTION_SET *ext_set)
{
	SEC_EXTENSTION_END_MARK *search_pattern;
	unsigned char *d_ptr, *end_ptr;
	unsigned int hash_only_idx = 0;

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump extension data ============> START\n", MOD);
	sec_signfmt_dump_buffer(ext, ext_len);
	SMSG(sec_info.bMsg, "[%s] Dump extension data ============> END\n", MOD);
#endif

	end_ptr = ext + ext_len;
	d_ptr = ext;

	while (d_ptr < end_ptr) {
		search_pattern = (SEC_EXTENSTION_END_MARK *) d_ptr;

		if (search_pattern->magic != SEC_EXTENSION_HEADER_MAGIC) {
			SMSG(true, "[%s] Image extension header magic wrong\n", MOD);
			return ERR_SIGN_FORMAT_EXT_HDR_MAGIC_WRONG;
		}

		switch (search_pattern->ext_type) {
		case SEC_EXT_HDR_CRYPTO:
			ext_set->crypto = (SEC_EXTENSTION_CRYPTO *) d_ptr;
			d_ptr += sizeof(SEC_EXTENSTION_CRYPTO);
			break;
		case SEC_EXT_HDR_FRAG_CFG:
			ext_set->frag = (SEC_FRAGMENT_CFG *) d_ptr;
			d_ptr += sizeof(SEC_FRAGMENT_CFG);
			ext_set->hash_only =
			    (SEC_EXTENSTION_HASH_ONLY **) ASF_MALLOC(ext_set->frag->frag_count *
								     sizeof(SEC_EXTENSTION_HASH_ONLY
									    *));
			break;
		case SEC_EXT_HDR_HASH_ONLY:
			ext_set->hash_only[hash_only_idx] = (SEC_EXTENSTION_HASH_ONLY *) d_ptr;
			d_ptr +=
			    sizeof(SEC_EXTENSTION_HASH_ONLY) +
			    get_hash_size(ext_set->hash_only[hash_only_idx]->sub_type);
			hash_only_idx++;
			break;
		case SEC_EXT_HDR_END_MARK:
			ext_set->end = (SEC_EXTENSTION_END_MARK *) d_ptr;
			d_ptr += sizeof(SEC_EXTENSTION_END_MARK);
			break;
		case SEC_EXT_HDR_HASH_SIG:
		default:
			SMSG(true, "[%s] Image header type not support %d\n", MOD,
			     search_pattern->ext_type);
			return ERR_SIGN_FORMAT_EXT_TYPE_NOT_SUPPORT;
		}
	}

	if (ext_set->crypto == NULL || ext_set->frag == NULL || ext_set->hash_only == NULL
	    || ext_set->end == NULL) {
		SMSG(true, "[%s] Some header is not searched\n", MOD);
		return ERR_SIGN_FORMAT_EXT_HDR_NOT_FOUND;
	}

	return SEC_OK;
}


/**************************************************************************
 *  FUNCTION To Get Hash Length
 **************************************************************************/
unsigned int sec_signfmt_get_hash_length_v3(SECURE_IMG_INFO_V3 *img_if, ASF_FILE fp,
					    SEC_IMG_HEADER *file_img_hdr, char *ext_buf)
{
	unsigned int crypto_hdr_offset = 0;
	unsigned int read_sz = 0;
	SEC_EXTENSTION_CRYPTO ext_crypto;
	SEC_EXTENSTION_CRYPTO *ext_crypto_ptr = NULL;
	unsigned int ext_crypto_size = sizeof(SEC_EXTENSTION_CRYPTO);

	/* get from seccfg's extension header */
	if (ASF_FILE_NULL == fp) {
		ext_crypto_ptr =
		    (SEC_EXTENSTION_CRYPTO *) (ext_buf + img_if->ext_offset +
					       img_if->header.v3.signature_length);

		return get_hash_size((SEC_CRYPTO_HASH_TYPE) ext_crypto_ptr->hash_type);
	}
	/* get from file's extension header */
	else {
		memset(&ext_crypto, 0x00, ext_crypto_size);

		/* seek to crypto header offset */
		crypto_hdr_offset = file_img_hdr->signature_offset + file_img_hdr->signature_length;

		ASF_SEEK_SET(fp, crypto_hdr_offset);

		/* read crypto header */
		read_sz = ASF_READ(fp, (char *)&ext_crypto, ext_crypto_size);
		if (ext_crypto_size != read_sz) {
			SMSG(true, "[%s] read sz '%d' != '%d'\n", MOD, read_sz, ext_crypto_size);
			return -1;
		}

		return get_hash_size((SEC_CRYPTO_HASH_TYPE) ext_crypto.hash_type);
	}
}

/**************************************************************************
 *  FUNCTION To Get Signature Length
 **************************************************************************/
unsigned int sec_signfmt_get_signature_length_v3(SECURE_IMG_INFO_V3 *img_if, ASF_FILE fp,
						 SEC_IMG_HEADER *file_img_hdr, char *ext_buf)
{
	unsigned int crypto_hdr_offset = 0;
	unsigned int read_sz = 0;
	SEC_EXTENSTION_CRYPTO ext_crypto;
	SEC_EXTENSTION_CRYPTO *ext_crypto_ptr = NULL;
	unsigned int ext_crypto_size = sizeof(SEC_EXTENSTION_CRYPTO);

	/* get from seccfg's extension header */
	if (ASF_FILE_NULL == fp) {
		ext_crypto_ptr =
		    (SEC_EXTENSTION_CRYPTO *) (ext_buf + img_if->ext_offset +
					       img_if->header.v3.signature_length);

		return get_signature_size((SEC_CRYPTO_SIGNATURE_TYPE) ext_crypto_ptr->sig_type);
	}
	/* get from file's extension header */
	else {
		memset(&ext_crypto, 0x00, ext_crypto_size);

		/* seek to crypto header offset */
		crypto_hdr_offset = file_img_hdr->signature_offset + file_img_hdr->signature_length;
		ASF_SEEK_SET(fp, crypto_hdr_offset);

		/* read crypto header */
		read_sz = ASF_READ(fp, (char *)&ext_crypto, ext_crypto_size);
		if (ext_crypto_size != read_sz) {
			SMSG(true, "[%s] read sz '%d' != '%d'\n", MOD, read_sz, ext_crypto_size);
			return -1;
		}

		return get_signature_size((SEC_CRYPTO_SIGNATURE_TYPE) ext_crypto.sig_type);
	}
}

/**************************************************************************
 *  FUNCTION To Get Extension Length
 **************************************************************************/
unsigned int sec_signfmt_get_extension_length_v3(SECURE_IMG_INFO_V3 *img_if, ASF_FILE fp,
						 SEC_IMG_HEADER *file_img_hdr)
{
	/* the extension include signature + hash + extension header */

	unsigned int file_size = 0;

	/* get from seccfg's extension header */
	if (ASF_FILE_NULL == fp)
		return img_if->ext_len;
	/* get from file's extension header */
	else {
		ASF_SEEK_END(fp, 0);
		file_size = ASF_FILE_POS(fp);

		return file_size - SEC_IMG_HEADER_SIZE - file_img_hdr->image_length;
	}
}


/**************************************************************************
 *  FUNCTION To Get Image Hash
 **************************************************************************/
int sec_signfmt_calculate_image_hash_v3(char *part_name, SECURE_IMG_INFO_V3 *img_if,
					char *final_hash_buf, unsigned int hash_len, char *ext_buf)
{
	unsigned int ret = SEC_OK;
	SEC_IMG_HEADER *img_hdr = (SEC_IMG_HEADER *) &img_if->header.v3;
	SEC_IMG_EXTENSTION_SET ext_set;
	unsigned int ext_hdr_offset = 0;
	unsigned int ext_hdr_len = 0;
	unsigned char *ext_hdr_buf = NULL;
	unsigned int hash_size = 0;
	unsigned int sig_size = 0;
	unsigned int i = 0;
	unsigned char *cal_hash_buf = NULL;
	unsigned int cal_hash_buf_len = 0;
	unsigned char *tmp_ptr = NULL;
	unsigned char *verify_data = NULL;
	unsigned int verify_data_len = 0;
	unsigned int real_chunk_size = 0;

	/* ======================== */
	/* init check */
	/* ======================== */
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump header ============> START\n", MOD);
	sec_signfmt_dump_buffer((unsigned char *) img_hdr, sizeof(SEC_IMG_HEADER));
	SMSG(sec_info.bMsg, "[%s] Dump header ============> END\n", MOD);
#endif

	if (SEC_IMG_MAGIC != img_hdr->magic_number) {
		SMSG(true, "[%s] magic number is invalid '0x%x'\n", MOD, img_hdr->magic_number);
		ret = ERR_SIGN_FORMAT_MAGIC_WRONG;
		goto _magic_wrong_err;
	}

	if (SEC_EXTENSION_MAGIC != img_hdr->sign_offset) {
		SMSG(true, "[%s] extension magic number is invalid '0x%x'\n", MOD,
		     img_hdr->sign_offset);
		ret = ERR_SIGN_FORMAT_MAGIC_WRONG;
		goto _magic_wrong_err;
	}

	/* ======================== */
	/* search for extension header */
	/* ======================== */
	memset(&ext_set, 0x00, sizeof(SEC_IMG_EXTENSTION_SET));
	ext_hdr_offset = img_if->ext_offset + img_hdr->signature_length;
	ext_hdr_len = img_if->ext_len - img_hdr->signature_length;
	ext_hdr_buf = (unsigned char *) (ext_buf + ext_hdr_offset);
	ret = sec_signfmt_search_extension(ext_hdr_buf, ext_hdr_len, &ext_set);
	if (SEC_OK != ret) {
		SMSG(true, "[%s] Image extension header not found\n", MOD);
		goto _ext_hdr_search_fail;
	}
	hash_size = get_hash_size((SEC_CRYPTO_HASH_TYPE) ext_set.crypto->hash_type);
	sig_size = get_signature_size((SEC_CRYPTO_SIGNATURE_TYPE) ext_set.crypto->sig_type);

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump ext hash value ============> START\n", MOD);
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		SMSG(sec_info.bMsg, "[%s] Dump EXT hash [%d]\n", MOD, i);
		sec_signfmt_dump_buffer(ext_set.hash_only[i]->hash_data, hash_size);
	}
	SMSG(sec_info.bMsg, "[%s] Dump ext hash value ============> END\n", MOD);
#endif

	/* ======================== */
	/* calculate each hash by chunk */
	/* ======================== */
	cal_hash_buf_len = hash_size * ext_set.frag->frag_count;
	cal_hash_buf = (unsigned char *) ASF_MALLOC(cal_hash_buf_len);
	if (NULL == cal_hash_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_cal_buf_fail;
	}
	memset(cal_hash_buf, 0x00, cal_hash_buf_len);
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] dump reset data\n", MOD);
	sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif
	tmp_ptr = cal_hash_buf;
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Total cal hash length is %d\n", MOD, cal_hash_buf_len);
#endif
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		memset(tmp_ptr, 0x00, hash_size);
		if (ext_set.frag->chunk_size == 0)
			real_chunk_size = ext_set.hash_only[i]->hash_len;
		else
			real_chunk_size = ext_set.frag->chunk_size;

		if (sec_signfmt_gen_hash_by_chunk
		    (ASF_FILE_NULL, part_name, ext_set.hash_only[i]->hash_offset,
		     ext_set.hash_only[i]->hash_len, tmp_ptr, ext_set.hash_only[i]->sub_type,
		     real_chunk_size) != 0) {
			ret = ERR_SIGN_FORMAT_CAL_HASH_BY_CHUNK_FAIL;
			goto _gen_hash_by_chunk_fail;
		}
#if DUMP_MORE_FOR_DEBUG
		SMSG(sec_info.bMsg, "[%s] Dump CAL hash right after: [%d], offset is 0x%x\n", MOD,
		     i, tmp_ptr);
		sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif
		tmp_ptr += hash_size;
	}

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump CAL hash right after all done, offset is 0x%x\n", MOD,
	     tmp_ptr);
	sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif


#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump cal hash value ============> START\n", MOD);
	tmp_ptr = cal_hash_buf;
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		SMSG(true, "[%s] Dump CAL hash [%d]\n", MOD, i);
		sec_signfmt_dump_buffer(tmp_ptr, hash_size);
		tmp_ptr += hash_size;
	}
	SMSG(sec_info.bMsg, "[%s] Dump cal hash value ============> END\n", MOD);
#endif

	/* ======================== */
	/* copy cal hash to extension header */
	/* ======================== */
	tmp_ptr = cal_hash_buf;
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		mcpy(ext_set.hash_only[i]->hash_data, tmp_ptr, hash_size);
		tmp_ptr += hash_size;
	}

	/* ======================== */
	/* compose final verify buffer */
	/* ======================== */
	verify_data_len = SEC_IMG_HEADER_SIZE + cal_hash_buf_len + ext_hdr_len;
	verify_data = (unsigned char *) ASF_MALLOC(verify_data_len);
	if (NULL == cal_hash_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_verify_buf_fail;
	}
	tmp_ptr = verify_data;
	/* copy header */
	mcpy(tmp_ptr, img_hdr, SEC_IMG_HEADER_SIZE);
	tmp_ptr += SEC_IMG_HEADER_SIZE;
	/* copy cal hash */
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		mcpy(tmp_ptr, cal_hash_buf + i * hash_size, hash_size);
		tmp_ptr += hash_size;
	}
	/* copy extension header */
	mcpy(tmp_ptr, ext_hdr_buf, ext_hdr_len);

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump verify data (%d):\n", MOD, verify_data_len);
	sec_signfmt_dump_buffer(verify_data, verify_data_len);
#endif

	/* ======================== */
	/* generate final hash */
	/* ======================== */

	/* hash */
	SMSG(sec_info.bMsg, "[%s] generate hash ...\n", MOD);
	ret = sec_hash(verify_data, verify_data_len, (unsigned char *) final_hash_buf, hash_len);
	if (SEC_OK != ret) {
		SMSG(true, "[%s] generate hash fail\n\n", MOD);
		ret = ERR_SIGN_FORMAT_GENERATE_HASH_FAIL;
		goto _hash_fail;
	}

	/* ================== */
	/* dump hash data     */
	/* ================== */
	SMSG(sec_info.bMsg, "[%s] dump hash data\n", MOD);
	dump_buf((unsigned char *) final_hash_buf, hash_len);

	SMSG(sec_info.bMsg, "[%s] generate hash pass\n\n", MOD);

_hash_fail:
	ASF_FREE(verify_data);
_malloc_verify_buf_fail:
_gen_hash_by_chunk_fail:
	ASF_FREE(cal_hash_buf);
_malloc_cal_buf_fail:
_ext_hdr_search_fail:
_magic_wrong_err:

	return ret;
}

/**************************************************************************
 *  FUNCTIONS To Verify File
 **************************************************************************/
int sec_signfmt_verify_file_v3(ASF_FILE fp, SEC_IMG_HEADER *img_hdr)
{
	unsigned int ret = SEC_OK;
	unsigned int final_hash_sig_len = 0;
	unsigned char *final_hash_sig_buf = NULL;
	unsigned int read_sz = 0;
	SEC_IMG_EXTENSTION_SET ext_set;
	unsigned int ext_hdr_offset = 0;
	unsigned int ext_hdr_len = 0;
	unsigned char *ext_hdr_buf = NULL;
	unsigned int file_size = 0;
	unsigned int hash_size = 0;
	unsigned int sig_size = 0;
	unsigned int i = 0;
	unsigned char *cal_hash_buf = NULL;
	unsigned int cal_hash_buf_len = 0;
	unsigned char *tmp_ptr = NULL;
	unsigned char *verify_data = NULL;
	unsigned int verify_data_len = 0;
	unsigned int real_chunk_size = 0;

	/* ======================== */
	/* init check */
	/* ======================== */
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump header ============> START\n", MOD);
	sec_signfmt_dump_buffer((unsigned char *) img_hdr, sizeof(SEC_IMG_HEADER));
	SMSG(sec_info.bMsg, "[%s] Dump header ============> END\n", MOD);
#endif

	if (SEC_IMG_MAGIC != img_hdr->magic_number) {
		SMSG(true, "[%s] magic number is invalid '0x%x'\n", MOD, img_hdr->magic_number);
		ret = ERR_SIGN_FORMAT_MAGIC_WRONG;
		goto _magic_wrong_err;
	}

	if (SEC_EXTENSION_MAGIC != img_hdr->sign_offset) {
		SMSG(true, "[%s] extension magic number is invalid '0x%x'\n", MOD,
		     img_hdr->sign_offset);
		ret = ERR_SIGN_FORMAT_MAGIC_WRONG;
		goto _magic_wrong_err;
	}

	/* ======================== */
	/* locate final signature and hash */
	/* ======================== */
	final_hash_sig_len = img_hdr->signature_length;
	final_hash_sig_buf = (unsigned char *) ASF_MALLOC(final_hash_sig_len);
	if (NULL == final_hash_sig_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_hash_sig_fail;
	}
	ASF_SEEK_SET(fp, img_hdr->signature_offset);

	read_sz = ASF_READ(fp, final_hash_sig_buf, final_hash_sig_len);
	if (final_hash_sig_len != read_sz) {
		SMSG(true, "[%s] read size '%d' != '%d'\n", MOD, read_sz, final_hash_sig_len);
		ret = ERR_FS_READ_SIZE_FAIL;
		goto _read_hash_sig_fail;
	}
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump sign and hash value ============> START\n", MOD);
	sec_signfmt_dump_buffer(final_hash_sig_buf, final_hash_sig_len);
	SMSG(sec_info.bMsg, "[%s] Dump sign and hash value ============> END\n", MOD);
#endif

	/* read file size */
	ASF_SEEK_END(fp, 0);
	file_size = ASF_FILE_POS(fp);

	/* ======================== */
	/* search for extension header */
	/* ======================== */
	memset(&ext_set, 0x00, sizeof(SEC_IMG_EXTENSTION_SET));
	ext_hdr_offset = SEC_IMG_HEADER_SIZE + img_hdr->image_length + img_hdr->signature_length;
	ext_hdr_len = file_size - ext_hdr_offset;
	ext_hdr_buf = (unsigned char *) ASF_MALLOC(ext_hdr_len);
	if (NULL == ext_hdr_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_ext_hdr_fail;
	}
	ASF_SEEK_SET(fp, ext_hdr_offset);

	read_sz = ASF_READ(fp, ext_hdr_buf, ext_hdr_len);
	if (ext_hdr_len != read_sz) {
		SMSG(true, "[%s] read size '%d' != '%d'\n", MOD, read_sz, ext_hdr_len);
		ret = ERR_FS_READ_SIZE_FAIL;
		goto _read_ext_hdr_fail;
	}
	ret = sec_signfmt_search_extension(ext_hdr_buf, ext_hdr_len, &ext_set);
	if (SEC_OK != ret) {
		SMSG(true, "[%s] Image extension header not found\n", MOD);
		goto _ext_hdr_search_fail;
	}

	hash_size = get_hash_size((SEC_CRYPTO_HASH_TYPE) ext_set.crypto->hash_type);
	sig_size = get_signature_size((SEC_CRYPTO_SIGNATURE_TYPE) ext_set.crypto->sig_type);

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump ext hash value ============> START\n", MOD);
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		SMSG(sec_info.bMsg, "[%s] Dump EXT hash [%d]\n", MOD, i);
		sec_signfmt_dump_buffer(ext_set.hash_only[i]->hash_data, hash_size);
	}
	SMSG(sec_info.bMsg, "[%s] Dump ext hash value ============> END\n", MOD);
#endif

	/* ======================== */
	/* calculate each hash by chunk */
	/* ======================== */
	cal_hash_buf_len = hash_size * ext_set.frag->frag_count;
	cal_hash_buf = (unsigned char *) ASF_MALLOC(cal_hash_buf_len);
	if (NULL == cal_hash_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_cal_buf_fail;
	}
	memset(cal_hash_buf, 0x00, cal_hash_buf_len);
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] dump reset data\n", MOD);
	sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif
	tmp_ptr = cal_hash_buf;
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Total cal hash length is %d\n", MOD, cal_hash_buf_len);
#endif
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		memset(tmp_ptr, 0x00, hash_size);
		if (ext_set.frag->chunk_size == 0)
			real_chunk_size = ext_set.hash_only[i]->hash_len;
		else
			real_chunk_size = ext_set.frag->chunk_size;

		if (sec_signfmt_gen_hash_by_chunk
		    (fp, NULL, SEC_IMG_HEADER_SIZE + ext_set.hash_only[i]->hash_offset,
		     ext_set.hash_only[i]->hash_len, tmp_ptr, ext_set.hash_only[i]->sub_type,
		     real_chunk_size) != 0) {
			ret = ERR_SIGN_FORMAT_CAL_HASH_BY_CHUNK_FAIL;
			goto _gen_hash_by_chunk_fail;
		}
#if DUMP_MORE_FOR_DEBUG
		SMSG(sec_info.bMsg, "[%s] Dump CAL hash right after: [%d], offset is 0x%x\n", MOD,
		     i, tmp_ptr);
		sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif
		tmp_ptr += hash_size;
	}

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump CAL hash right after all done, offset is 0x%x\n", MOD,
	     tmp_ptr);
	sec_signfmt_dump_buffer(cal_hash_buf, cal_hash_buf_len);
#endif


#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump cal hash value ============> START\n", MOD);
	tmp_ptr = cal_hash_buf;
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		SMSG(sec_info.bMsg, "[%s] Dump CAL hash [%d]\n", MOD, i);
		sec_signfmt_dump_buffer(tmp_ptr, hash_size);
		tmp_ptr += hash_size;
	}
	SMSG(sec_info.bMsg, "[%s] Dump cal hash value ============> END\n", MOD);
#endif

	/* ======================== */
	/* compose final verify buffer */
	/* ======================== */
	verify_data_len = SEC_IMG_HEADER_SIZE + cal_hash_buf_len + ext_hdr_len;
	verify_data = (unsigned char *) ASF_MALLOC(verify_data_len);
	if (NULL == cal_hash_buf) {
		ret = ERR_FS_READ_BUF_ALLOCATE_FAIL;
		goto _malloc_verify_buf_fail;
	}
	tmp_ptr = verify_data;
	/* copy header */
	mcpy(tmp_ptr, img_hdr, SEC_IMG_HEADER_SIZE);
	tmp_ptr += SEC_IMG_HEADER_SIZE;
	/* copy cal hash */
	for (i = 0; i < ext_set.frag->frag_count; i++) {
		mcpy(tmp_ptr, cal_hash_buf + i * hash_size, hash_size);
		tmp_ptr += hash_size;
	}
	/* copy extension header */
	mcpy(tmp_ptr, ext_hdr_buf, ext_hdr_len);

#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump verify data (%d):\n", MOD, verify_data_len);
	sec_signfmt_dump_buffer(verify_data, verify_data_len);
#endif
#if DUMP_MORE_FOR_DEBUG
	SMSG(sec_info.bMsg, "[%s] Dump signature data (%d):\n", MOD, sig_size);
	sec_signfmt_dump_buffer(final_hash_sig_buf, sig_size);
#endif

	osal_verify_lock();

	/* ======================== */
	/* verify buffer */
	/* ======================== */
	SMSG(sec_info.bMsg, "[%s] verify (lock)...\n", MOD);
	ret = sec_verify(verify_data, verify_data_len, final_hash_sig_buf, sig_size);
	if (SEC_OK != ret) {
		osal_verify_unlock();
		SMSG(true, "[%s] verify fail (unlock), ret is %d\n\n", MOD, ret);
		goto _verify_fail;
	}

	osal_verify_unlock();

	SMSG(sec_info.bMsg, "[%s] verify pass (unlock)\n\n", MOD);

_verify_fail:
	ASF_FREE(verify_data);
_malloc_verify_buf_fail:
_gen_hash_by_chunk_fail:
	ASF_FREE(cal_hash_buf);
_malloc_cal_buf_fail:
_ext_hdr_search_fail:
_read_ext_hdr_fail:
	ASF_FREE(ext_hdr_buf);
_malloc_ext_hdr_fail:
_read_hash_sig_fail:
	ASF_FREE(final_hash_sig_buf);
_malloc_hash_sig_fail:
_magic_wrong_err:

	return ret;
}
