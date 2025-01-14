/* SPDX-FileCopyrightText: 2001-2002 NaN Holding BV. All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup avi
 *
 * This is external code. Converts between AVI and MPEG/JPEG.
 */

#include <cstdlib>
#include <cstring>

#include "AVI_avi.h"

#include "MEM_guardedalloc.h"

#include "BLI_math_base.h"
#include "IMB_imbuf.hh"

#include <jerror.h>
#include <jpeglib.h>

#include "avi_mjpeg.h"

static void jpegmemdestmgr_build(j_compress_ptr cinfo, uchar *buffer, size_t bufsize);
static void jpegmemsrcmgr_build(j_decompress_ptr dinfo, const uchar *buffer, size_t bufsize);

static size_t numbytes;

static void add_huff_table(j_decompress_ptr dinfo,
                           JHUFF_TBL **htblptr,
                           const UINT8 *bits,
                           const size_t bits_size,
                           const UINT8 *val,
                           const size_t val_size)
{
  if (*htblptr == nullptr) {
    *htblptr = jpeg_alloc_huff_table((j_common_ptr)dinfo);
  }

  memcpy((*htblptr)->bits, bits, min_zz(sizeof((*htblptr)->bits), bits_size));
  memcpy((*htblptr)->huffval, val, min_zz(sizeof((*htblptr)->huffval), val_size));

  /* Initialize sent_table false so table will be written to JPEG file. */
  (*htblptr)->sent_table = false;
}

/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */

static void std_huff_tables(j_decompress_ptr dinfo)
{
  static const UINT8 bits_dc_luminance[17] = {
      /* 0-base */
      0,
      0,
      1,
      5,
      1,
      1,
      1,
      1,
      1,
      1,
      0,
      0,
      0,
      0,
      0,
      0,
      0,
  };
  static const UINT8 val_dc_luminance[] = {
      0,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,
      11,
  };

  static const UINT8 bits_dc_chrominance[17] = {
      /* 0-base */
      0,
      0,
      3,
      1,
      1,
      1,
      1,
      1,
      1,
      1,
      1,
      1,
      0,
      0,
      0,
      0,
      0,
  };
  static const UINT8 val_dc_chrominance[] = {
      0,
      1,
      2,
      3,
      4,
      5,
      6,
      7,
      8,
      9,
      10,
      11,
  };

  static const UINT8 bits_ac_luminance[17] = {
      /* 0-base */
      0,
      0,
      2,
      1,
      3,
      3,
      2,
      4,
      3,
      5,
      5,
      4,
      4,
      0,
      0,
      1,
      0x7d,
  };
  static const UINT8 val_ac_luminance[] = {
      0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
      0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
      0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
      0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
      0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
      0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
      0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
      0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
      0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3,
      0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
      0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
  };
  static const UINT8 bits_ac_chrominance[17] = {
      /* 0-base */
      0,
      0,
      2,
      1,
      2,
      4,
      4,
      3,
      4,
      7,
      5,
      4,
      4,
      0,
      1,
      2,
      0x77,
  };
  static const UINT8 val_ac_chrominance[] = {
      0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61,
      0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
      0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18,
      0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
      0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
      0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
      0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
      0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
      0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
      0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
      0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
  };

  add_huff_table(dinfo,
                 &dinfo->dc_huff_tbl_ptrs[0],
                 bits_dc_luminance,
                 sizeof(bits_dc_luminance),
                 val_dc_luminance,
                 sizeof(val_dc_luminance));
  add_huff_table(dinfo,
                 &dinfo->ac_huff_tbl_ptrs[0],
                 bits_ac_luminance,
                 sizeof(bits_ac_luminance),
                 val_ac_luminance,
                 sizeof(val_ac_luminance));
  add_huff_table(dinfo,
                 &dinfo->dc_huff_tbl_ptrs[1],
                 bits_dc_chrominance,
                 sizeof(bits_dc_chrominance),
                 val_dc_chrominance,
                 sizeof(val_dc_chrominance));
  add_huff_table(dinfo,
                 &dinfo->ac_huff_tbl_ptrs[1],
                 bits_ac_chrominance,
                 sizeof(bits_ac_chrominance),
                 val_ac_chrominance,
                 sizeof(val_ac_chrominance));
}

static int Decode_JPEG(uchar *inBuffer, uchar *outBuffer, uint width, uint height, size_t bufsize)
{
  jpeg_decompress_struct dinfo;
  jpeg_error_mgr jerr;

  (void)width; /* unused */

  numbytes = 0;

  dinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&dinfo);
  jpegmemsrcmgr_build(&dinfo, inBuffer, bufsize);
  jpeg_read_header(&dinfo, true);
  if (dinfo.dc_huff_tbl_ptrs[0] == nullptr) {
    std_huff_tables(&dinfo);
  }
  dinfo.out_color_space = JCS_RGB;
  dinfo.dct_method = JDCT_IFAST;

  jpeg_start_decompress(&dinfo);

  size_t rowstride = dinfo.output_width * dinfo.output_components;
  for (size_t y = 0; y < dinfo.output_height; y++) {
    jpeg_read_scanlines(&dinfo, (JSAMPARRAY)&outBuffer, 1);
    outBuffer += rowstride;
  }
  jpeg_finish_decompress(&dinfo);

  if (dinfo.output_height >= height) {
    return 0;
  }

  inBuffer += numbytes;
  jpegmemsrcmgr_build(&dinfo, inBuffer, bufsize - numbytes);

  numbytes = 0;
  jpeg_read_header(&dinfo, true);
  if (dinfo.dc_huff_tbl_ptrs[0] == nullptr) {
    std_huff_tables(&dinfo);
  }

  jpeg_start_decompress(&dinfo);
  rowstride = dinfo.output_width * dinfo.output_components;
  for (size_t y = 0; y < dinfo.output_height; y++) {
    jpeg_read_scanlines(&dinfo, (JSAMPARRAY)&outBuffer, 1);
    outBuffer += rowstride;
  }
  jpeg_finish_decompress(&dinfo);
  jpeg_destroy_decompress(&dinfo);

  return 1;
}

static void Compress_JPEG(
    int quality, uchar *outbuffer, const uchar *inBuffer, int width, int height, size_t bufsize)
{
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  uchar marker[60];

  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  jpegmemdestmgr_build(&cinfo, outbuffer, bufsize);

  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_colorspace(&cinfo, JCS_YCbCr);

  jpeg_set_quality(&cinfo, quality, true);

  cinfo.dc_huff_tbl_ptrs[0]->sent_table = true;
  cinfo.dc_huff_tbl_ptrs[1]->sent_table = true;
  cinfo.ac_huff_tbl_ptrs[0]->sent_table = true;
  cinfo.ac_huff_tbl_ptrs[1]->sent_table = true;

  cinfo.comp_info[0].component_id = 0;
  cinfo.comp_info[0].v_samp_factor = 1;
  cinfo.comp_info[1].component_id = 1;
  cinfo.comp_info[2].component_id = 2;

  cinfo.write_JFIF_header = false;

  jpeg_start_compress(&cinfo, false);

  int i = 0;
  marker[i++] = 'A';
  marker[i++] = 'V';
  marker[i++] = 'I';
  marker[i++] = '1';
  marker[i++] = 0;
  while (i < 60) {
    marker[i++] = 32;
  }

  jpeg_write_marker(&cinfo, JPEG_APP0, marker, 60);

  i = 0;
  while (i < 60) {
    marker[i++] = 0;
  }

  jpeg_write_marker(&cinfo, JPEG_COM, marker, 60);

  size_t rowstride = cinfo.image_width * cinfo.input_components;
  for (size_t y = 0; y < cinfo.image_height; y++) {
    jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&inBuffer, 1);
    inBuffer += rowstride;
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
}

static void interlace(uchar *to, uchar *from, int width, int height)
{
  size_t i, rowstride = width * 3;

  for (i = 0; i < height; i++) {
    if (i & 1) {
      memcpy(&to[i * rowstride], &from[(i / 2 + height / 2) * rowstride], rowstride);
    }
    else {
      memcpy(&to[i * rowstride], &from[(i / 2) * rowstride], rowstride);
    }
  }
}

static void deinterlace(int odd, uchar *to, uchar *from, int width, int height)
{
  size_t i, rowstride = width * 3;

  for (i = 0; i < height; i++) {
    if ((i & 1) == odd) {
      memcpy(&to[(i / 2 + height / 2) * rowstride], &from[i * rowstride], rowstride);
    }
    else {
      memcpy(&to[(i / 2) * rowstride], &from[i * rowstride], rowstride);
    }
  }
}

void *avi_converter_from_mjpeg(AviMovie *movie, int stream, uchar *buffer, const size_t *size)
{
  int deint;
  uchar *buf;

  (void)stream; /* unused */

  buf = static_cast<uchar *>(imb_alloc_pixels(movie->header->Height,
                                              movie->header->Width,
                                              3,
                                              sizeof(uchar),
                                              true,
                                              "avi.avi_converter_from_mjpeg 1"));
  if (!buf) {
    return nullptr;
  }

  deint = Decode_JPEG(buffer, buf, movie->header->Width, movie->header->Height, *size);

  MEM_freeN(buffer);

  if (deint) {
    buffer = static_cast<uchar *>(imb_alloc_pixels(movie->header->Height,
                                                   movie->header->Width,
                                                   3,
                                                   sizeof(uchar),
                                                   true,
                                                   "avi.avi_converter_from_mjpeg 2"));
    if (buffer) {
      interlace(buffer, buf, movie->header->Width, movie->header->Height);
    }
    MEM_freeN(buf);

    buf = buffer;
  }

  return buf;
}

void *avi_converter_to_mjpeg(AviMovie *movie, int stream, uchar *buffer, size_t *size)
{
  uchar *buf;
  size_t bufsize = *size;

  numbytes = 0;
  *size = 0;

  buf = static_cast<uchar *>(imb_alloc_pixels(movie->header->Height,
                                              movie->header->Width,
                                              3,
                                              sizeof(uchar),
                                              true,
                                              "avi.avi_converter_to_mjpeg 1"));
  if (!buf) {
    return nullptr;
  }

  if (!movie->interlace) {
    Compress_JPEG(movie->streams[stream].sh.Quality / 100,
                  buf,
                  buffer,
                  movie->header->Width,
                  movie->header->Height,
                  bufsize);
    *size += numbytes;
  }
  else {
    deinterlace(movie->odd_fields, buf, buffer, movie->header->Width, movie->header->Height);
    MEM_freeN(buffer);

    buffer = buf;
    buf = static_cast<uchar *>(imb_alloc_pixels(movie->header->Height,
                                                movie->header->Width,
                                                3,
                                                sizeof(uchar),
                                                true,
                                                "avi.avi_converter_to_mjpeg 1"));

    if (buf) {
      Compress_JPEG(movie->streams[stream].sh.Quality / 100,
                    buf,
                    buffer,
                    movie->header->Width,
                    movie->header->Height / 2,
                    bufsize / 2);
      *size += numbytes;
      numbytes = 0;
      Compress_JPEG(movie->streams[stream].sh.Quality / 100,
                    buf + *size,
                    buffer + size_t(movie->header->Height / 2) * size_t(movie->header->Width) * 3,
                    movie->header->Width,
                    movie->header->Height / 2,
                    bufsize / 2);
      *size += numbytes;
    }
  }

  MEM_freeN(buffer);
  return buf;
}

/* Compression from memory */

static void jpegmemdestmgr_init_destination(j_compress_ptr cinfo)
{
  (void)cinfo; /* unused */
}

static boolean jpegmemdestmgr_empty_output_buffer(j_compress_ptr cinfo)
{
  (void)cinfo; /* unused */
  return true;
}

static void jpegmemdestmgr_term_destination(j_compress_ptr cinfo)
{
  numbytes -= cinfo->dest->free_in_buffer;

  MEM_freeN(cinfo->dest);
}

static void jpegmemdestmgr_build(j_compress_ptr cinfo, uchar *buffer, size_t bufsize)
{
  cinfo->dest = static_cast<jpeg_destination_mgr *>(
      MEM_mallocN(sizeof(*(cinfo->dest)), "avi.jpegmemdestmgr_build"));

  cinfo->dest->init_destination = jpegmemdestmgr_init_destination;
  cinfo->dest->empty_output_buffer = jpegmemdestmgr_empty_output_buffer;
  cinfo->dest->term_destination = jpegmemdestmgr_term_destination;

  cinfo->dest->next_output_byte = buffer;
  cinfo->dest->free_in_buffer = bufsize;

  numbytes = bufsize;
}

/* Decompression from memory */

static void jpegmemsrcmgr_init_source(j_decompress_ptr dinfo)
{
  (void)dinfo;
}

static boolean jpegmemsrcmgr_fill_input_buffer(j_decompress_ptr dinfo)
{
  uchar *buf = (uchar *)dinfo->src->next_input_byte - 2;

  /* if we get called, must have run out of data */
  WARNMS(dinfo, JWRN_JPEG_EOF);

  buf[0] = (JOCTET)0xFF;
  buf[1] = (JOCTET)JPEG_EOI;

  dinfo->src->next_input_byte = buf;
  dinfo->src->bytes_in_buffer = 2;

  return true;
}

static void jpegmemsrcmgr_skip_input_data(j_decompress_ptr dinfo, long skip_count)
{
  if (dinfo->src->bytes_in_buffer < skip_count) {
    skip_count = dinfo->src->bytes_in_buffer;
  }

  dinfo->src->next_input_byte += skip_count;
  dinfo->src->bytes_in_buffer -= skip_count;
}

static void jpegmemsrcmgr_term_source(j_decompress_ptr dinfo)
{
  numbytes -= dinfo->src->bytes_in_buffer;

  MEM_freeN(dinfo->src);
}

static void jpegmemsrcmgr_build(j_decompress_ptr dinfo, const uchar *buffer, size_t bufsize)
{
  dinfo->src = static_cast<jpeg_source_mgr *>(
      MEM_mallocN(sizeof(*(dinfo->src)), "avi.jpegmemsrcmgr_build"));

  dinfo->src->init_source = jpegmemsrcmgr_init_source;
  dinfo->src->fill_input_buffer = jpegmemsrcmgr_fill_input_buffer;
  dinfo->src->skip_input_data = jpegmemsrcmgr_skip_input_data;
  dinfo->src->resync_to_restart = jpeg_resync_to_restart;
  dinfo->src->term_source = jpegmemsrcmgr_term_source;

  dinfo->src->bytes_in_buffer = bufsize;
  dinfo->src->next_input_byte = buffer;

  numbytes = bufsize;
}
