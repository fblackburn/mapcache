/******************************************************************************
 * $Id$
 *
 * Project:  MapServer
 * Purpose:  MapCache tile caching support file: UTFGRID format
 * Author:   François Blackburn and the MapServer team.
 *
 ******************************************************************************
 * Copyright (c) 1996-2011 Regents of the University of Minnesota.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies of this Software or works derived from this Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "mapcache.h"
#include <apr_strings.h>
#include <json.h>

void _mapcache_imageio_utfgrid_decode_to_image(mapcache_context *r, mapcache_buffer *buffer, mapcache_image *img)
{
  json_object *utfGridObject;
  json_object *gridJson;
  json_object *utfData;
  json_object *curDataObject;

  int nbchar, bufferBaseSize, nbData, dataSize;
  char* charBuffer = NULL;
  char* out_string = NULL;
  char* value = NULL;
  char* utfKey = NULL;
  char* utfDataValue = NULL;

  int i, j;
  int nbByte;

  int32_t char_code;

  utfGridObject = json_tokener_parse((char*)buffer->buf);

  gridJson = json_object_object_get(utfGridObject,"grid");
  utfData = json_object_object_get(utfGridObject,"data");

  struct json_object_iterator iterEnd = json_object_iter_end(utfData);
  struct json_object_iterator iterCur = json_object_iter_begin(utfData);

  img->h = json_object_array_length(gridJson);
  img->w = msGetNumGlyphs(json_object_get_string(json_object_array_get_idx(gridJson,0)));

  //We can assume that the needed buffer to build the data will be of a minimum of width * height bytes
  bufferBaseSize = img->w * img->h;

  img->data = (int32_t *)calloc(bufferBaseSize, sizeof(int32_t));
  apr_pool_cleanup_register(r->pool, img->data, (void*)free, apr_pool_cleanup_null) ;

  //We create a buffer of 11 bytes to be used with the msgetnextglyph function.
  out_string = (char*)apr_palloc(r->pool, 11 * sizeof(char*));

  for(i = 0;i<img->h;i++)
  {
    value = json_object_get_string(json_object_array_get_idx(gridJson,i));

    for(j = 0; j<img->w;j++)
    {
      nbByte = msGetNextGlyph(&value,out_string);
      char_code = get_utf_char_code(out_string, nbByte);

      memcpy((void*)(&img->data[(i*img->w+j)*4]),(void*)(&char_code),sizeof(int32_t));
    }
  }

  nbData = json_object_object_length(utfData);
  img->nb_utf_item = nbData;
  img->utfGridValue = (mapcache_utf_data*)apr_pcalloc(r->pool, nbData*sizeof(mapcache_utf_data));

  i = 0;

  while(!json_object_iter_equal(&iterCur,&iterEnd))
  {
    img->utfGridValue[i].utfValue = utf_encode(i+1);

    curDataObject = json_object_iter_peek_value(&iterCur);
    utfDataValue = json_object_get_string(curDataObject);
    dataSize = strlen(utfDataValue);

    charBuffer = (char*) apr_pcalloc(r->pool, dataSize * sizeof(char));

    memcpy((void*)charBuffer,(void*)utfDataValue, dataSize);
    img->utfGridValue[i].utfData = charBuffer;

    utfKey = json_object_iter_peek_name(&iterCur);
    dataSize = strlen(utfKey);
    charBuffer = (char*) apr_pcalloc(r->pool, (dataSize+1) * sizeof(char));

    memcpy((void*)charBuffer, (void*)utfKey, dataSize);
    img->utfGridValue[i].utfItem = charBuffer;

    i++;
    json_object_iter_next(&iterCur);
  }

  json_object_put(utfGridObject);
  json_object_put(gridJson);
  json_object_put(utfData);
  json_object_put(curDataObject);
}

int utf_encode(int valueKey)
{
  valueKey += 32;
  
  if(valueKey >= 34)
    valueKey ++;
  if(valueKey >= 92)
    valueKey ++;

  return valueKey;
}

int utf_decode(int valueKey)
{
  if(valueKey >= 92)
    valueKey --;
  if(valueKey >= 34)
    valueKey --;

  valueKey -= 32;

  return valueKey;
}

static unsigned char mask[] = { 0, 0x7F, 0x1F, 0x0F, 0x07, 0x03, 0x01};

int32_t get_utf_char_code(char* utf_char, int nb_byte)
{
  if (nb_byte > 1)
  {
    int i;
    wchar_t value;

    value = (wchar_t)(utf_char[0] & mask[nb_byte]);

    for(i=1; i < nb_byte; i++)
    {
      value <<= 6;
      value |= (wchar_t)(utf_char[i] & 0x3F);
    }
    return (int32_t)value;
  }
  else 
  {
    return (int32_t)utf_char[0];
  }
}

mapcache_image* _mapcache_imageio_utfgrid_decode(mapcache_context *r, mapcache_buffer *buffer)
{
  mapcache_image *img = mapcache_image_utfgrid_create(r);
  _mapcache_imageio_utfgrid_decode_to_image(r, buffer, img);
  if(GC_HAS_ERROR(r)) {
    return NULL;
  }
  return img;
}

mapcache_buffer* _mapcache_imageio_utfgrid_encode(mapcache_context *ctx, mapcache_image *img, mapcache_image_format *format)
{
  int byte_size, i, j;
  mapcache_buffer* grid_line = NULL;
  mapcache_utf_data* updatedData;
  int elementInGrid;

  int32_t *char_code;

  elementInGrid = utfgridCleanData(ctx, img, &updatedData);
  
  char* utf_buffer = (char*)apr_pcalloc(ctx->pool,7 * sizeof(char));

  json_object *trunk = json_object_new_object();

  json_object *grid = json_object_new_array();
  json_object *keys = json_object_new_array();
  
  json_object *tempString = NULL;

  for(i=0;i<img->h;i++)
  {
    grid_line = mapcache_buffer_create(img->w, ctx->pool);

    for(j=0;j<img->w;j++)
    {
      char_code = &img->data[((img->w * i)+j)*4];

      byte_size = wc_to_utf8(utf_buffer, *char_code);
      mapcache_buffer_append(grid_line, byte_size, utf_buffer);
    }
    tempString = json_object_new_string_len(grid_line->buf, grid_line->size);
    json_object_array_add(grid,tempString);
  }

  json_object_object_add(trunk, "grid", grid);

  mapcache_utf_data *utf_data;

  json_object *utf_data_buffer;

  json_object *utf_data_element = json_object_new_object();

  json_object *utf_key_array = json_object_new_array();
  json_object *utf_key;

  //Add the empty key at the beginning of index
  utf_key = json_object_new_string("");
  json_object_array_add(utf_key_array,utf_key);

  for(i=0;i<elementInGrid;i++)
  {
    utf_data = &img->utfGridValue[i];

    utf_data_buffer = json_tokener_parse(utf_data->utfData);
    json_object_object_add(utf_data_element,utf_data->utfItem,utf_data_buffer);

    utf_key = json_object_new_string(utf_data->utfItem);
    json_object_array_add(utf_key_array, utf_key);
  }

  json_object_object_add(trunk,"keys",utf_key_array);
  json_object_object_add(trunk,"data",utf_data_element);

  mapcache_buffer *json_file = mapcache_buffer_create(strlen(json_object_to_json_string(trunk)),ctx->pool);
  mapcache_buffer_append(json_file,strlen(json_object_to_json_string(trunk)),json_object_to_json_string(trunk));

  json_object_put(trunk);

  json_object_put(keys);
  json_object_put(utf_key_array);
  json_object_put(utf_key);
  json_object_put(utf_data_element);
  json_object_put(grid);

  if(elementInGrid > 0)
    json_object_put(utf_data_buffer);

  return json_file;

}

static mapcache_buffer* _mapcache_imageio_utfgrid_create_empty(mapcache_context *ctx, mapcache_image_format *format,
    size_t width, size_t height, unsigned int color)
{

}

mapcache_image_format* mapcache_imageio_create_utfgrid_format(apr_pool_t *pool, char *name)
{
  mapcache_image_format_utfgrid *format = apr_pcalloc(pool, sizeof(mapcache_image_format_utfgrid));
  format->format.name = name;
  format->format.extension = apr_pstrdup(pool,"json");
  format->format.mime_type = apr_pstrdup(pool,"application/json");
  format->format.metadata = apr_table_make(pool,3);
  format->format.create_empty_image = _mapcache_imageio_utfgrid_create_empty;
  format->format.write = _mapcache_imageio_utfgrid_encode;
  format->format.type = GC_UTFGRID;
  return (mapcache_image_format*)format;
}

/*
** NOTE: This code is taken from mapstring.c in the mapserver source code
**
** Returns the next glyph in string and advances *in_ptr to the next
** character.
**
** If out_string is not NULL then the character (bytes) is copied to this
** buffer and null-terminated. out_string must be a pre-allocated buffer of
** at least 11 bytes.
**
** The function returns the number of bytes in this glyph.
**
** This function treats 3 types of glyph encodings:
*   - as an html entity, for example &#123; , &#x1af; , or &eacute;
*   - as an utf8 encoded character
*   - if utf8 decoding fails, as a raw character
*
** This function mimics the character decoding function used in gdft.c of
* libGD. It is necessary to have the same behaviour, as input strings must be
* split into the same glyphs as what gd does.
**
** In UTF-8, the number of leading 1 bits in the first byte specifies the
** number of bytes in the entire sequence.
** Source: http://www.cl.cam.ac.uk/~mgk25/unicode.html#utf-8
**
** U-00000000 U-0000007F: 0xxxxxxx
** U-00000080 U-000007FF: 110xxxxx 10xxxxxx
** U-00000800 U-0000FFFF: 1110xxxx 10xxxxxx 10xxxxxx
** U-00010000 U-001FFFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
** U-00200000 U-03FFFFFF: 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
** U-04000000 U-7FFFFFFF: 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
*/
int msGetNextGlyph(const char **in_ptr, char *out_string)
{
  unsigned char in;
  int numbytes=0;
  unsigned int unicode;
  int i;

  in = (unsigned char)**in_ptr;

  if (in == 0)
    return -1;  /* Empty string */

  if (in < 0xC0) {
    /*
    * Handles properly formed UTF-8 characters between
    * 0x01 and 0x7F.  Also treats \0 and naked trail
    * bytes 0x80 to 0xBF as valid characters representing
    * themselves.
    */
    /*goto end of loop to return just the char*/
  } else if (in < 0xE0) {
    if (((*in_ptr)[1]& 0xC0) == 0x80) {
      if(out_string) {
        out_string[0]=in;
        out_string[1]=(*in_ptr)[1];
        out_string[2]='\0';
      }
      *in_ptr+=2;
      return 2; /*110xxxxx 10xxxxxx*/
    }
  } else if (in < 0xF0) {
    if (((*in_ptr)[1]& 0xC0) == 0x80 && ((*in_ptr)[2]& 0xC0) == 0x80) {
      if(out_string) {
        out_string[0]=in;
        *in_ptr+=numbytes;
        out_string[1]=(*in_ptr)[1];
        out_string[2]=(*in_ptr)[2];
        out_string[3]='\0';
      }
      *in_ptr+=3;
      return 3;   /* 1110xxxx 10xxxxxx 10xxxxxx */
    }
  } else if (in < 0xF8) {
    if (((*in_ptr)[1]& 0xC0) == 0x80 && ((*in_ptr)[2]& 0xC0) == 0x80
        && ((*in_ptr)[3]& 0xC0) == 0x80) {
      if(out_string) {
        out_string[0]=in;
        out_string[1]=(*in_ptr)[1];
        out_string[2]=(*in_ptr)[2];
        out_string[3]=(*in_ptr)[3];
        out_string[4]='\0';
      }
      *in_ptr+=4;
      return 4;   /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
    }
  } else if (in < 0xFC) {
    if (((*in_ptr)[1]& 0xC0) == 0x80 && ((*in_ptr)[2]& 0xC0) == 0x80
        && ((*in_ptr)[3]& 0xC0) == 0x80 && ((*in_ptr)[4]& 0xC0) == 0x80) {
      if(out_string) {
        out_string[0]=in;
        out_string[1]=(*in_ptr)[1];
        out_string[2]=(*in_ptr)[2];
        out_string[3]=(*in_ptr)[3];
        out_string[4]=(*in_ptr)[4];
        out_string[5]='\0';
      }
      *in_ptr+=5;
      return 5;   /* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
    }
  } else if (in < 0xFE) {
    if (((*in_ptr)[1]& 0xC0) == 0x80 && ((*in_ptr)[2]& 0xC0) == 0x80
        && ((*in_ptr)[3]& 0xC0) == 0x80 && ((*in_ptr)[4]& 0xC0) == 0x80
        && ((*in_ptr)[5]& 0xC0) == 0x80) {
      if(out_string) {
        out_string[0]=in;
        out_string[1]=(*in_ptr)[1];
        out_string[2]=(*in_ptr)[2];
        out_string[3]=(*in_ptr)[3];
        out_string[4]=(*in_ptr)[4];
        out_string[5]=(*in_ptr)[5];
        out_string[6]='\0';
      }
      *in_ptr+=6;
      return 6;   /* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
    }
  }

  if (out_string) {
    out_string[0]=in;
    out_string[1] = '\0';   /* 0xxxxxxx */
  }
  (*in_ptr)++;
  return 1;
}

/*
** NOTE: This code is taken from mapstring.c in the mapserver source code
**
** Returns the number of glyphs in string
*/
int msGetNumGlyphs(const char *in_ptr)
{
  int numchars=0;

  while( msGetNextGlyph(&in_ptr, NULL) != -1 )
    numchars++;

  return numchars;
}

int wc_to_utf8(char *utf8char, int32_t char_code)
{
  int len=0;

  if(char_code < 0x80) {
    utf8char[len++] = (char)(char_code);
  }
  else if(char_code < 0x800){
    utf8char[len++] = 0xc0 | (char_code >> 6);
    utf8char[len++] = 0x80 | (char_code & 0x3f);
  }
  else if(char_code < 0x10000){
    utf8char[len++] = 0xe0 | (char_code >> 12);
    utf8char[len++] = 0x80 | ((char_code >> 6) & 0x3f);
    utf8char[len++] = 0x80 | (char_code & 0x3f);
  }
  else if(char_code < 0x200000){
    utf8char[len++] = 0xf0 | (char_code >> 18);
    utf8char[len++] = 0x80 | ((char_code >> 12)& 0x3f);
    utf8char[len++] = 0x80 | ((char_code >> 6) & 0x3f);
    utf8char[len++] = 0x80 | (char_code & 0x3f);
  }
  else if(char_code < 0x4000000){
    utf8char[len++] = 0xf8 | (char_code >> 24);
    utf8char[len++] = 0x80 | ((char_code >> 18)& 0x3f);
    utf8char[len++] = 0x80 | ((char_code >> 12)& 0x3f);
    utf8char[len++] = 0x80 | ((char_code >> 6) & 0x3f);
    utf8char[len++] = 0x80 | (char_code & 0x3f);
  }

  return len;
}

/*Update a char by the new one in the data*/

void utfgridUpdateChar(mapcache_image *img, uint32_t oldChar, uint32_t newChar)
{
  int i,bufferLength;
  int32_t *char_code;

  bufferLength = img->h * img->w;

  for(i=0;i<bufferLength;i++){
    char_code = &img->data[i*4];

    if(*char_code == oldChar)
      *char_code = newChar;
  }
}

/*Compress utfgrid data to show only relevant information*/

int utfgridCleanData(mapcache_context *ctx, mapcache_image *img, mapcache_utf_data **dataPointer)
{
  int * usedChar;
  int i,bufferLength,itemFound,dataCounter;
  unsigned char utfvalue;
  mapcache_utf_data* updatedData;
  int32_t *char_code;

  bufferLength = img->h * img->w;

  usedChar = (int*)apr_pcalloc(ctx->pool, img->nb_utf_item*sizeof(int));

  for(i=0;i<img->nb_utf_item;i++){
    usedChar[i]=0;
  }

  itemFound=0;

  for(i=0;i<bufferLength;i++)
  {
    char_code = &img->data[i*4];
    if(utf_decode(*char_code) != 0 && usedChar[utf_decode(*char_code)-1]==0)
    {
      itemFound++;
      usedChar[utf_decode(*char_code)-1] = 1;
    }
  }
  
  updatedData = *dataPointer;

  updatedData = (mapcache_utf_data*)apr_pcalloc(ctx->pool, itemFound*sizeof(mapcache_utf_data));
  dataCounter = 0;

  for(i=0; i< img->nb_utf_item; i++){
    if(usedChar[utf_decode(img->utfGridValue[i].utfValue)-1]==1){
      memcpy((void*)(&updatedData[dataCounter]),(void*)(&img->utfGridValue[i]),sizeof(mapcache_utf_data));

      utfvalue=utf_encode(dataCounter+1);

      utfgridUpdateChar(img,updatedData[dataCounter].utfValue,utfvalue);
      updatedData[dataCounter].utfValue = utfvalue;

      dataCounter++;
      usedChar[utf_decode(img->utfGridValue[i].utfValue)-1] = 0;
    }
  }

  return itemFound;
}
