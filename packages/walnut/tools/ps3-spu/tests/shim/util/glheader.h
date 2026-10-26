/* Stands in for Mesa's util/glheader.h so texcompress_s3tc_tmp.h builds alone (tests/mesa-dxt.c):
 * the GL types and enums it uses, nothing else */
#ifndef SHIM_GLHEADER_H
#define SHIM_GLHEADER_H
#include <stdint.h>
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLint;
typedef short GLshort;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef void GLvoid;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT		0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT	0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT	0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT	0x83F3
#endif
