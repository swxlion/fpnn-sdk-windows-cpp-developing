#ifndef FPNN_CPP_SDK_EMBEDDED_TYPE_H
#define FPNN_CPP_SDK_EMBEDDED_TYPE_H

/*
*		!!! VERY IMPORTANT NOTICES !!!
*
*	All comments in this file are very important for using other programming language to encapsulate
*	the C++SDK in upper applications or SDKs.
*
*	Vistual Studio is unfriendly with Chinese comments without BOM header, but other platforms are
*	unfriendly with BOM header. To many codes are same for the Windwos version, Linux version, MacOS
*	version, and other POSIX versions.
*
*	I don't want to translate the Chinese comments here anymore. For related notes, please refer to
*	the corresponding location of the Linux version.
*/
namespace fpnn
{
	typedef void (*EmbedRecvNotifyDelegate)(uint64_t connectionId, const void* buffer, int length);
}

#endif