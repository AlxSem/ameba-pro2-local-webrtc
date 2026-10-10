/* Local WebRTC example signaling transport.
 * Integrates with the Amazon/Realtek example via SignalingController API.
 * Lab-only: HTTP/1.1 plaintext on trusted LAN, single viewer, no authentication.
 * Source-derived helper functions at end retain original Amazon license.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "core_json.h"
#include "signaling_controller.h"
#include "logging.h"

#ifndef LOCAL_SIGNALING_HOST
#define LOCAL_SIGNALING_HOST "192.168.1.100" /* Change to PC IPv4 before build. */
#endif
#ifndef LOCAL_SIGNALING_PORT
#define LOCAL_SIGNALING_PORT 8080
#endif

#define LOCAL_HTTP_MAX 12288
#define LOCAL_POLL_MS 200
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_KEY "type"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_OFFER "offer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_ANSWER "answer"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_CONTENT_KEY "sdp"
#define SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_NEWLINE_ENDING "\\n"

static const char kRemoteId[] = "local-browser";

/* One independent TCP connection per request.  Allows candidate callbacks on other tasks. */
static int local_send_all( int fd, const char * data, size_t length )
{
    while( length )
    {
        int n = send( fd, data, length, 0 );
        if( n <= 0 ) return -1;
        data += n;
        length -= ( size_t ) n;
    }
    return 0;
}

/* The local Node bridge always sends Content-Length and Connection: close.
 * Response includes neither chunked transfer encoding nor redirects. */
static int local_http( const char * method, const char * path,
                     const char * body, size_t bodylen,
                     char * result, size_t capacity, size_t * outputlen )
{
    struct sockaddr_in addr;
    struct timeval timeout = { 4, 0 };
    char header[ 400 ];
    char * buffer = NULL;
    size_t used = 0, total = 0, headlen = 0, contentlen = 0;
    int fd = -1, n, status = -1;
    char * sep = NULL;

    if( outputlen ) *outputlen = 0;
    if( result == NULL || capacity == 0 || ( bodylen && !body ) ) return -1;
    fd = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if( fd < 0 ) return -1;
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
    setsockopt( fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof( timeout ) );
    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_port = htons( LOCAL_SIGNALING_PORT );
    addr.sin_addr.s_addr = inet_addr( LOCAL_SIGNALING_HOST );
    if( addr.sin_addr.s_addr == INADDR_NONE ) goto cleanup;
    if( connect( fd, ( struct sockaddr * ) &addr, sizeof( addr ) ) != 0 ) goto cleanup;

    n = snprintf( header, sizeof( header ),
                  "%s %s HTTP/1.1\r\nHost: " LOCAL_SIGNALING_HOST ":%d\r\n"
                  "Content-Type: application/json\r\nContent-Length: %u\r\n"
                  "Connection: close\r\n\r\n",
                  method, path, LOCAL_SIGNALING_PORT, ( unsigned ) bodylen );
    if( n <= 0 || ( size_t ) n >= sizeof( header ) ||
        local_send_all( fd, header, ( size_t ) n ) != 0 ||
        ( bodylen && local_send_all( fd, body, bodylen ) != 0 ) ) goto cleanup;

    buffer = malloc( LOCAL_HTTP_MAX + 1024 );
    if( !buffer ) goto cleanup;
    while( used < LOCAL_HTTP_MAX + 1023 )
    {
        n = recv( fd, buffer + used, LOCAL_HTTP_MAX + 1023 - used, 0 );
        if( n <= 0 ) break;
        used += ( size_t ) n;
        buffer[ used ] = '\0';
        if( !sep )
        {
            sep = strstr( buffer, "\r\n\r\n" );
            if( sep )
            {
                char * p;
                char * lim = sep;
                headlen = ( size_t ) ( sep - buffer ) + 4;
                if( strncmp( buffer, "HTTP/1.1 ", 9 ) &&
                    strncmp( buffer, "HTTP/1.0 ", 9 ) ) goto cleanup;
                status = atoi( buffer + 9 );
                /* Reject nonconforming server replies; never treat truncation as success. */
                p = strstr( buffer, "Content-Length:" );
                if( !p || p > lim ) goto cleanup;
                contentlen = ( size_t ) strtoul( p + 15, NULL, 10 );
                if( contentlen >= capacity || contentlen > LOCAL_HTTP_MAX ) goto cleanup;
            }
        }
        if( sep && used >= headlen + contentlen ) break;
    }
    if( !sep || used < headlen + contentlen ) goto cleanup;
    total = contentlen;
    if( total ) memcpy( result, buffer + headlen, total );
    result[ total ] = '\0';
    if( outputlen ) *outputlen = total;
cleanup:
    if( fd >= 0 ) closesocket( fd );
    if( buffer ) free( buffer );
    return ( status >= 200 && status < 300 && sep && used >= headlen + contentlen ) ? status : -1;
}

static int local_find_type( const char * json, size_t length,
                          SignalingTypeMessage_t * messageType )
{
    const char * value = NULL;
    size_t len = 0;
    if( JSON_Validate( json, length ) != JSONSuccess ||
        JSON_Search( json, length, "type", 4, &value, &len ) != JSONSuccess ) return -1;
    if( len == 5 && !memcmp( value, "offer", 5 ) )
        *messageType = SIGNALING_TYPE_MESSAGE_SDP_OFFER;
    else if( len == 9 && !memcmp( value, "candidate", 9 ) )
        *messageType = SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE;
    else if( len == 3 && !memcmp( value, "bye", 3 ) ) return 1;
    else return -1;
    return 0;
}

SignalingControllerResult_t SignalingController_Init( SignalingControllerContext_t * ctx,
                                                      const SSLCredentials_t * creds )
{
    ( void ) creds;
    if( !ctx ) return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    memset( ctx, 0, sizeof( *ctx ) );
    ctx->connectionState = SIGNALING_CONTROLLER_STATE_INITED;
    return SIGNALING_CONTROLLER_RESULT_OK;
}

SignalingControllerResult_t SignalingController_RefreshIceServerConfigs( SignalingControllerContext_t * ctx )
{
    if( !ctx ) return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    ctx->iceServerConfigsCount = 0;
    return SIGNALING_CONTROLLER_RESULT_OK;
}

SignalingControllerResult_t SignalingController_QueryIceServerConfigs( SignalingControllerContext_t * ctx,
                                                                       IceServerConfig_t ** servers,
                                                                       size_t * count )
{
    if( !ctx || !servers || !count ) return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    *servers = ctx->iceServerConfigs;
    *count = 0;
    return SIGNALING_CONTROLLER_RESULT_OK;
}

SignalingControllerResult_t SignalingController_StartListening( SignalingControllerContext_t * ctx,
                                                                const SignalingControllerConnectInfo_t * config )
{
    char reply[ 256 ];
    size_t replylen = 0;
    int rc;
    if( !ctx || !config ) return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    ctx->messageReceivedCallback = config->messageReceivedCallback;
    ctx->pMessageReceivedCallbackData = config->pMessageReceivedCallbackData;
    LogInfo( ( "Local WebRTC signaling: connecting to " LOCAL_SIGNALING_HOST ":%d", LOCAL_SIGNALING_PORT ) );

reconnect:
    for( ;; )
    {
        rc = local_http( "POST", "/camera/hello", "{}", 2,
                       reply, sizeof( reply ), &replylen );
        if( rc == 200 ) break;
        LogWarn( ( "Local WebRTC signaling: PC not reachable, retrying" ) );
        vTaskDelay( pdMS_TO_TICKS( 2000 ) );
    }
    ctx->connectionState = SIGNALING_CONTROLLER_STATE_CONNECTED;
    if( ctx->connectionStateCallback )
        ctx->connectionStateCallback( SIGNALING_CONTROLLER_STATE_CONNECTED,
                                      ctx->pConnectionStateCallbacCustomContext );

    for( ;; )
    {
        SignalingMessage_t message;
        SignalingTypeMessage_t messageType;
        size_t length = 0;
        int kind;
        rc = local_http( "GET", "/camera/poll", NULL, 0,
                       ctx->signalingRxMessageBuffer,
                       sizeof( ctx->signalingRxMessageBuffer ), &length );
        if( rc == 200 && length )
        {
            kind = local_find_type( ctx->signalingRxMessageBuffer, length, &messageType );
            if( kind == 0 && ctx->messageReceivedCallback )
            {
                memset( &message, 0, sizeof( message ) );
                message.messageType = messageType;
                message.pRemoteClientId = kRemoteId;
                message.remoteClientIdLength = sizeof( kRemoteId ) - 1;
                message.pMessage = ctx->signalingRxMessageBuffer;
                message.messageLength = length;
                ctx->messageReceivedCallback( &message, ctx->pMessageReceivedCallbackData );
            }
            else if( kind == 1 ) LogInfo( ( "Local WebRTC signaling: viewer left" ) );
        }
        else if( rc < 0 )
        {
            ctx->connectionState = SIGNALING_CONTROLLER_STATE_DISCONNECTED;
            if( ctx->connectionStateCallback )
                ctx->connectionStateCallback( SIGNALING_CONTROLLER_STATE_DISCONNECTED,
                                              ctx->pConnectionStateCallbacCustomContext );
            /* Restart handshake after LAN outage. */
            goto reconnect;
        }
        vTaskDelay( pdMS_TO_TICKS( LOCAL_POLL_MS ) );
    }
}

SignalingControllerResult_t SignalingController_SendMessage( SignalingControllerContext_t * ctx,
                                                             SignalingControllerEventMessage_t * event )
{
    const char * type = NULL;
    const char * format = NULL;
    char reply[ 128 ];
    size_t ignored = 0;
    size_t n;
    int written, rc;
    char * body;
    SignalingControllerResult_t result = SIGNALING_CONTROLLER_RESULT_FAIL;
    if( !ctx || !event || !event->eventContent.pDecodeMessage )
        return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    if( event->eventContent.messageType == SIGNALING_TYPE_MESSAGE_SDP_ANSWER )
    {
        type = "answer";
        format = "{\"type\":\"answer\",\"payload\":{\"type\":\"answer\",\"sdp\":\"%.*s\"}}";
    }
    else if( event->eventContent.messageType == SIGNALING_TYPE_MESSAGE_ICE_CANDIDATE )
    {
        type = "candidate";
        format = "{\"type\":\"candidate\",\"payload\":%.*s}";
    }
    if( !type ) return SIGNALING_CONTROLLER_RESULT_FAIL;
    n = event->eventContent.decodeMessageLength;
    if( n > 8192 ) return SIGNALING_CONTROLLER_RESULT_FAIL;
    body = malloc( n + 128 );
    if( !body ) return SIGNALING_CONTROLLER_RESULT_FAIL;
    written = snprintf( body, n + 128, format, ( int ) n, event->eventContent.pDecodeMessage );
    if( written > 0 && ( size_t ) written < n + 128 )
    {
        rc = local_http( "POST", "/camera/send", body, ( size_t ) written,
                       reply, sizeof( reply ), &ignored );
        if( rc == 200 ) result = SIGNALING_CONTROLLER_RESULT_OK;
    }
    free( body );
    /* On failure app_common frees the candidate buffer itself.
     * Invoking its completion callback on failure would double-free it. */
    if( result == SIGNALING_CONTROLLER_RESULT_OK && event->onCompleteCallback )
        event->onCompleteCallback( SIGNALING_CONTROLLER_EVENT_STATUS_SENT_DONE,
                                   event->pOnCompleteCallbackContext );
    return result;
}

/* Original helper implementations from the supplied Realtek example,
 * used unchanged for the AWS-compatible SDP JSON escaping conventions.
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0.
 */
SignalingControllerResult_t SignalingController_ExtractSdpMessageFromSignalingMessage( const char * pEventMessage,
                                                                                       size_t eventMessageLength,
                                                                                       uint8_t isSdpOffer,
                                                                                       const char ** ppSdpMessage,
                                                                                       size_t * pSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    JSONStatus_t jsonResult;
    size_t start = 0, next = 0;
    JSONPair_t pair = { 0 };
    uint8_t isContentFound = 0;
    const char * pTargetTypeValue = isSdpOffer == 1 ? SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_OFFER : SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_VALUE_ANSWER;

    if( ( pEventMessage == NULL ) ||
        ( ppSdpMessage == NULL ) ||
        ( pSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pEventMessage: %p, ppSdpMessage: %p, pSdpMessageLength: %p", pEventMessage, ppSdpMessage, pSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Validate( pEventMessage, eventMessageLength );

        if( jsonResult != JSONSuccess )
        {
            LogWarn( ( "Input message is not valid JSON message, result: %d, message(%d): %.*s",
                       jsonResult,
                       eventMessageLength,
                       ( int ) eventMessageLength,
                       pEventMessage ) );
            ret = SIGNALING_CONTROLLER_RESULT_FAIL;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        jsonResult = JSON_Iterate( pEventMessage, eventMessageLength, &start, &next, &pair );

        while( jsonResult == JSONSuccess )
        {
            if( ( strncmp( pair.key, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_TYPE_KEY, pair.keyLength ) == 0 ) &&
                ( strncmp( pair.value, pTargetTypeValue, pair.valueLength ) != 0 ) )
            {
                /* It's not expected SDP offer message. */
                LogWarn( ( "Message type \"%.*s\" is not SDP target type \"%s\"",
                           ( int ) pair.valueLength, pair.value,
                           pTargetTypeValue ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                break;
            }
            else if( strncmp( pair.key, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_CONTENT_KEY, pair.keyLength ) == 0 )
            {
                *ppSdpMessage = pair.value;
                *pSdpMessageLength = pair.valueLength;
                isContentFound = 1;
                break;
            }
            else
            {
                /* Skip unknown attributes. */
            }

            jsonResult = JSON_Iterate( pEventMessage, eventMessageLength, &start, &next, &pair );
        }
    }

    if( ( ret == SIGNALING_CONTROLLER_RESULT_OK ) && !isContentFound )
    {
        LogWarn( ( "No target content found in event message, result: %d, SDP target type \"%s\", message(%d): %.*s",
                   jsonResult,
                   pTargetTypeValue,
                   eventMessageLength,
                   ( int ) eventMessageLength,
                   pEventMessage ) );
        ret = SIGNALING_CONTROLLER_RESULT_FAIL;
    }

    return ret;
}

SignalingControllerResult_t SignalingController_DeserializeSdpContentNewline( const char * pSdpMessage,
                                                                              size_t sdpMessageLength,
                                                                              char * pFormalSdpMessage,
                                                                              size_t * pFormalSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    const char * pCurSdp = pSdpMessage, * pNext = NULL;
    char * pCurOutput = NULL;
    size_t lineLength = 0, outputLength = 0;

    if( ( pSdpMessage == NULL ) ||
        ( pFormalSdpMessage == NULL ) ||
        ( pFormalSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSdpMessage: %p, pFormalSdpMessage: %p, pFormalSdpMessageLength: %p", pSdpMessage, pFormalSdpMessage, pFormalSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pCurOutput = pFormalSdpMessage;

        while( ( pNext = strstr( pCurSdp, SIGNALING_CONTROLLER_SDP_EVENT_MESSAGE_NEWLINE_ENDING ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( ( lineLength >= 2 ) &&
                ( pCurSdp[ lineLength - 2 ] == '\\' ) && ( pCurSdp[ lineLength - 1 ] == 'r' ) )
            {
                lineLength -= 2;
            }

            if( *pFormalSdpMessageLength < outputLength + lineLength + 2 )
            {
                LogWarn( ( "Buffer space is not enough to store formal SDP message, buffer size: %u, SDP message(%u): %.*s",
                           *pFormalSdpMessageLength,
                           sdpMessageLength,
                           ( int ) sdpMessageLength,
                           pSdpMessage ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                break;
            }

            memcpy( pCurOutput, pCurSdp, lineLength );
            pCurOutput += lineLength;
            *pCurOutput++ = '\r';
            *pCurOutput++ = '\n';
            outputLength += lineLength + 2;

            pCurSdp = pNext + 2;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        *pFormalSdpMessageLength = outputLength;
    }

    return ret;
}

SignalingControllerResult_t SignalingController_SerializeSdpContentNewline( const char * pSdpMessage,
                                                                            size_t sdpMessageLength,
                                                                            char * pEventSdpMessage,
                                                                            size_t * pEventSdpMessageLength )
{
    SignalingControllerResult_t ret = SIGNALING_CONTROLLER_RESULT_OK;
    const char * pCurSdp = pSdpMessage, * pNext, * pTail;
    char * pCurOutput = pEventSdpMessage;
    size_t lineLength, outputLength = 0;
    int writtenLength;

    if( ( pSdpMessage == NULL ) ||
        ( pEventSdpMessage == NULL ) ||
        ( pEventSdpMessageLength == NULL ) )
    {
        LogError( ( "Invalid input, pSdpMessage: %p, pEventSdpMessage: %p, pEventSdpMessageLength: %p", pSdpMessage, pEventSdpMessage, pEventSdpMessageLength ) );
        ret = SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        pTail = pSdpMessage + sdpMessageLength;

        while( ( pNext = memchr( pCurSdp, '\n', pTail - pCurSdp ) ) != NULL )
        {
            lineLength = pNext - pCurSdp;

            if( ( lineLength > 0 ) &&
                ( pCurSdp[ lineLength - 1 ] == '\r' ) )
            {
                lineLength--;
            }
            else
            {
                /* do nothing, coverity happy. */
            }

            if( *pEventSdpMessageLength < outputLength + lineLength + 4 )
            {
                LogError( ( "The output buffer length(%u) is too small to store serialized %u bytes message.",
                            *pEventSdpMessageLength,
                            sdpMessageLength ) );
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                break;
            }

            writtenLength = snprintf( pCurOutput, *pEventSdpMessageLength - outputLength, "%.*s\\r\\n",
                                      ( int ) lineLength,
                                      pCurSdp );
            if( writtenLength < 0 )
            {
                ret = SIGNALING_CONTROLLER_RESULT_FAIL;
                LogError( ( "snprintf returns fail %d", writtenLength ) );
                break;
            }
            else
            {
                outputLength += lineLength + 4;
                pCurOutput += lineLength + 4;
            }

            pCurSdp = pNext + 1;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        if( pTail > pCurSdp )
        {
            /* Copy the ending string. */
            lineLength = pTail - pCurSdp;
            memcpy( pCurOutput, pCurSdp, lineLength );

            outputLength += lineLength;
            pCurOutput += lineLength;
            pCurSdp += lineLength;
        }
    }

    if( ret == SIGNALING_CONTROLLER_RESULT_OK )
    {
        *pEventSdpMessageLength = outputLength;
    }

    return ret;
}


SignalingControllerResult_t SignalingController_SetConnectionStateCallback( SignalingControllerContext_t * ctx,
                                                                            SignalingControllerConnectionStateCallback_t cb,
                                                                            void * userdata )
{
    if( !ctx ) return SIGNALING_CONTROLLER_RESULT_BAD_PARAM;
    ctx->connectionStateCallback = cb;
    ctx->pConnectionStateCallbacCustomContext = userdata;
    return SIGNALING_CONTROLLER_RESULT_OK;
}
