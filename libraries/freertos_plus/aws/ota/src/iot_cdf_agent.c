/*
 * Amazon FreeRTOS OTA Agent V1.0.0
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */
/* MQTT includes. */
#include "iot_mqtt.h"

/* Standard library includes. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Set up logging for this demo. */
#include "iot_demo_logging.h" 

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* Internal header file for shared definitions. */
#include "aws_clientcredential_keys.h"
#include "aws_clientcredential.h"
#include "aws_iot_ota_agent.h"
#include "iot_cdf_agent.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"     /*lint !e537 intentional include of all interfaces used by this file. */
#include "timers.h"       /*lint !e537 intentional include of all interfaces used by this file. */
#include "task.h"         /*lint !e537 intentional include of all interfaces used by this file. */
#include "event_groups.h" /*lint !e537 intentional include of all interfaces used by this file. */
#include "queue.h"
#include "semphr.h"
#include "jsmn.h"

#define _CR_SUB_TOPIC_COUNT      1
#define _CR_PUB_TOPIC_COUNT      1
#define _CERT_ROTATION_DELAY_MS  ( 20000 )
#define _CR_TOPIC_PREFIX         "certificate/rotation"
#define _CR_TOPIC_PREFIX_LEN     ( sizeof( _CR_TOPIC_PREFIX ) + 1 )
#define _CR_TOPIC_SUFFIX_LEN     20
#define _CR_TOPIC_LEN            ( sizeof(clientcredentialIOT_THING_NAME) +_CR_TOPIC_SUFFIX_LEN +\
                                   _CR_TOPIC_PREFIX_LEN )

#define _CR_GET_RESPONSE_STR_BEG "\"newCertificatePem\": \""
#define _CR_GET_RESPONSE_STR_END "-----END CERTIFICATE-----"
#define _CR_GET_RESPONSE_BEG_OFFSET  ( 22 )
#define _CR_GET_NEW_CERT_ID_STR_BEG "\"newCertificateId\": \""
#define _CR_GET_OLD_CERT_ID_STR_BEG "\"oldCertificateId\": \""

#define _CR_ACK_TIMEOUT          ((TickType_t) 5000)
#define _SYNC_WAIT_MS            ( 1000 )

#define _CR_ACK_RESPONSE_STR_ERROR     "\"error\": \"}"
#define _CR_ACK_RESPONSE_LEN     ( 12 )

#define _PUBLISH_PAYLOAD_BUFFER_LENGTH   (_CR_CSR_SIZE + 50)
#define _MAX_MQTT_PUBLISH_ATTEMPTS       ( 2 ) 
#define _MAX_MQTT_GET_CERT_ATTEMPTS      ( 2 ) 
#define _MAX_WAIT_CDF_TASK_ATTEMPTS      ( 4 ) 
#define _MAX_TOKEN_COUNT                 ( 40 )
#define _CDF_JOB_JSON_MATCH_COUNT        ( 5 )
#define _MAX_JSON_VAL_LEN                 ( 200 )
#define _MAX_JSON_KEY_LEN                 ( 100 )

#define _FINGERPRINT_LENGTH                ( 64 )
int debug_something = false;

/**
 * @brief The timeout for MQTT operations in this demo.
 */
#define _MQTT_TIMEOUT_MS                          ( 5000 )
/**
 * @brief The maximum number of times each PUBLISH in this demo will be retried.
 */
#define _PUBLISH_RETRY_LIMIT                      ( 10 )

/**
 * @brief A PUBLISH message is retried if no response is received within this
 * time.
 */
#define _PUBLISH_RETRY_MS                         ( 1000 )

/**
 * @brief Format string of PUBLISH acknowledgement messages in this demo.
 */
#define _ACKNOWLEDGEMENT_MESSAGE_FORMAT           "Client has received PUBLISH %.*s from server."

/**
 * @brief Size of the buffers that hold acknowledgement messages in this demo.
 */
#define _ACKNOWLEDGEMENT_MESSAGE_BUFFER_LENGTH    ( sizeof( _ACKNOWLEDGEMENT_MESSAGE_FORMAT ) + 2 )

/**
 * @brief The topic name on which acknowledgement messages for incoming publishes
 * should be published.
 */
#define _ACKNOWLEDGEMENT_TOPIC_NAME               _CR_TOPIC_PREFIX "/acknowledgements"

/**
 * @brief The length of #_ACKNOWLEDGEMENT_TOPIC_NAME.
 */
#define _ACKNOWLEDGEMENT_TOPIC_NAME_LENGTH        ( ( uint16_t ) ( sizeof( _ACKNOWLEDGEMENT_TOPIC_NAME ) - 1 ) )
 
int strFind(char * buffer, char * match);

static int newCertInProgress;
/* This is the CDF statistics structure to hold useful info. */
typedef struct ota_agent_statistics
{
    uint32_t ulCDF_PacketsReceived;  /* Number of CDF packets received by the MQTT callback. */
    uint32_t ulCDF_PacketsQueued;    /* Number of CDF packets queued by the MQTT callback. */
    uint32_t ulCDF_PacketsProcessed; /* Number of CDF packets processed by the CDF task. */
    uint32_t ulCDF_PacketsDropped;   /* Number of CDF packets dropped due to congestion. */
    uint32_t ulCDF_PublishFailures;  /* Number of MQTT publish failures. */
} CDF_AgentStatistics_t;

/* The CDF agent is a singleton today. The structure keeps it nice and organized. */

typedef struct cdf_agent_context
{
    CDF_State_t eState;                                     /* State of the CDF agent. */
    uint8_t pcThingName[ cdfconfigMAX_THINGNAME_LEN + 1U ]; /* Thing name + zero terminator. */
    void * pMqttConnection;                                 /* Publish/subscribe MQTT connection shared with OTA agent. */
    SemaphoreHandle_t xStartCertRotateSemaphore;            /* Semaphore given by CDF Custom Job callback to start 
                                                             * cert rotation, taken in CDF task 
                                                             */
    CDF_AgentStatistics_t xStatistics;                      /* CDF agent statistics block. */
    cdf_Api_t xCdfApi;                                      /* CDF Api calls */
    pxOTACustomJobCallback_t xOTACustomJobCallback;         /* OTA Custom Job Callback, saved at init then called by CDF 
                                                             * custom job callback if job is not a CDF job
                                                             */
} CDF_AgentContext_t;

static void prvCDF_RotateCertTask( void * pvUnused );

int parseJsonCdfJob(char *json_str)
{
    char value[_MAX_JSON_VAL_LEN];
    char key[_MAX_JSON_KEY_LEN];

    int i;
    int r;
    int match_countdown = _CDF_JOB_JSON_MATCH_COUNT;

    jsmn_parser p;
    jsmntok_t t[_MAX_TOKEN_COUNT];

    jsmn_init(&p);

    r = jsmn_parse(&p, json_str, strlen(json_str), t, sizeof(t)/(sizeof(t[0])));

    if (r < 0)
    {
        IotLogError("parseJsonCdfAgent: JSON parsing failed. JSON = %s", json_str);
        return false;
    }

    /* Assume the top-level element is an object */
    if (r < 1 || t[0].type != JSMN_OBJECT)
    {
        IotLogError("parseJsonCdfAgent: JSON parsing expected Object");
        return false;
    }

    for (i = 1; i < r; i++)
    {
        jsmntok_t json_value = t[i+1];
        jsmntok_t json_key = t[i];

        int val_length = json_value.end - json_value.start;
        int key_length = json_key.end - json_key.start;

        int idx;

        for (idx = 0; idx < val_length && idx < (_MAX_JSON_VAL_LEN - 1); idx++)
        {
            value[idx] = json_str[json_value.start + idx ];
        }

        for (idx = 0; idx < key_length && idx < (_MAX_JSON_KEY_LEN - 1); idx++)
        {
            key[idx] = json_str[json_key.start + idx];
        }

        key[key_length] = '\0';
        value[val_length] = '\0';

#ifdef DEBUG_CUSTOM_JOB_JSON
        IotLogInfo("parseJsonCdfAgent: key %s", key);
        IotLogInfo("parseJsonCdfAgent: key len %d, max key len %d", key_length, _MAX_JSON_KEY_LEN);

        IotLogInfo("parseJsonCdfAgent: value %s", value);
        IotLogInfo("parseJsonCdfAgent: value len %d, max value len %d", val_length, _MAX_JSON_VAL_LEN);
        IotClock_SleepMs( 500 );
#endif
        /* 
         * Make sure the number of compares matches the 
         * _CDF_JOB_JSON_MATCH_COUNT and a match_countdown--
         * happends for each.
         */
        if (!strcmp(key, "operation"))
        {   
            if (!strcmp(value, "RotateCertificates"))
            {   
                match_countdown--;
            }
        }

        if (!strcmp(key, "subscribe"))
        {   
            if (!strcmp(value, "cdf/certificates/{thingName}/get/+"))
            {   
                match_countdown--;
            }
            if (!strcmp(value, "cdf/certificates/{thingName}/ack/+"))
            {   
                match_countdown--;
            }
        }

        if (!strcmp(key, "publish"))
        {
            if (!strcmp(value, "cdf/certificates/{thingName}/get"))
            {
                match_countdown--;
            }
            if (!strcmp(value, "cdf/certificates/{thingName}/ack"))
            {
                match_countdown--;
            }
        }
        i++;
    }

    if (!match_countdown)
    {
        return true;
    }
    else
    {
        return false;
    }
}

int cleanupJsonStr(char * json_str, int max_length)
{
    int bracket_cnt = 0;
    int char_cnt = 0;
    int rc = true;
    char *cp;
    char *lastRightBracket;

    cp = json_str;

#ifdef OFF_LOW_DEBUG
    IotLogError("cleanupJsonStr: JSON = %s.", cp);
#endif
    /* 
     * Verify all characters printable ascii \n, or \r.
     * Verify left and right bracket count match.
     * Null terminate string after last right bracket.
     */
    while (*cp)
    {
        if (*cp < ' ' || *cp > '~')
        {
            if (*cp != '\r' && *cp != '\n')
            {
                /*
                    * Invalid string
                    */
                rc = false;
                IotLogError("cleanupJsonStr: non-printable character in the string");
                break;
            }
        }
        if (*cp == '{')
        {
            /* if junk characters at the end of JSON include '{'
             * then this will fail when it should not, but this
             * is the best option. Appropriate retries of whatever
             * is generating the JSON must happen.
             */
            bracket_cnt++;
#ifdef OFF_LOW_DEBUG
            IotLogError("2 %c, %d", *cp, (int) (cp - json_str));
#endif
        }
        if (*cp == '}')
        {
            /* if junk characters at the end of JSON include '}'
             * then this will fail when it should not, but this
             * is the best option. Appropriate retries of whatever
             * is generating the JSON must happen.
             */
            bracket_cnt--;
            lastRightBracket = cp;
#ifdef OFF_LOW_DEBUG
            IotLogError("3 %c, %d", *cp, (int) (cp - json_str));
#endif
        }
        char_cnt++;
        cp++;
        if (char_cnt > max_length)
        {
            /* reached the end before finding a null character */
#ifdef OFF_LOW_DEBUG
            IotLogError("4 %c, %d", *cp, (int) (cp - json_str));
#endif
            break;
        }
#ifdef OFF_LOW_DEBUG
        else 
        {
            IotLogError("5 %c, %d", *cp, (int) (cp - json_str));
            IotClock_SleepMs( 5 );
        }
#endif
    }

    if (bracket_cnt)
    {
        IotLogError("cleanupJsonStr: asymetric brackets");
        rc = false;
    }
    else
    {
        /* Null terminate the string after the JSON for 
         * the case of junk characters following the JSON
         * in mqtt receive message buffers.
         */
        *(lastRightBracket + 1) = '\0';
    }

    return (rc);
}


/* Put defaults in case customer callbacks are Null. */
static uint8_t prvCDF_DefaultWriteState (CDF_STATE val)
{
    return EXIT_FAILURE;
}

static CDF_STATE prvCDF_DefaultReadState ()
{
    return CDF_STATE_UNKNOWN;
}

static uint8_t prvCDF_DefaultPutTempDeviceCert (char *cert_str)
{
    return EXIT_FAILURE;
}

static char * prvCDF_DefaultGetTempDeviceCert (void)
{
    return NULL;
}

static uint8_t prvCDF_DefaultPutDeviceCert (char *cert_str)
{
    return EXIT_FAILURE;
}

static char * prvCDF_DefaultGetDeviceCert (void)
{
    return NULL;
}

static uint8_t prvCDF_DefaultPutDevicePrivateKey (char *private_key_str)
{
    return EXIT_FAILURE;
}

static char * prvCDF_DefaultGetDevicePrivateKey ( void )
{
    return NULL;
}

static uint8_t prvCDF_DefaultPutCSR (char *csr_str)
{
    return EXIT_FAILURE;
}

static char * prvCDF_DefaultGetCSR ( void )
{
    return NULL;
}

static int prvCDF_DefaultRegisterDevice ( void )
{
    return EXIT_FAILURE;
}

static OTA_JobParseErr_t prvCDFDefaultCustomJobCallback( const char * pcJSON, uint32_t ulMsgLen )
{
    return eOTA_JobParseErr_NonConformingJobDoc;
}

#define CDF_JOB_API_DEFAULT_INITIALIZER \
{ \
    .xWriteCDFStateNVM          = prvCDF_DefaultWriteState, \
    .xReadCDFStateNVM           = prvCDF_DefaultReadState, \
    .xPutTempDeviceCert         = prvCDF_DefaultPutTempDeviceCert, \
    .xGetTempDeviceCert         = prvCDF_DefaultGetTempDeviceCert, \
    .xPutDeviceCert             = prvCDF_DefaultPutDeviceCert, \
    .xGetDeviceCert             = prvCDF_DefaultGetDeviceCert,\
    .xPutDevicePrivateKey       = prvCDF_DefaultPutDevicePrivateKey, \
    .xGetDevicePrivateKey       = prvCDF_DefaultGetDevicePrivateKey, \
    .xPutCSR                    = prvCDF_DefaultPutCSR, \
    .xGetCSR                    = prvCDF_DefaultGetCSR, \
    .xRegisterDevice            = prvCDF_DefaultRegisterDevice \
} 

static CDF_AgentContext_t xCDF_Agent =
{
    .eState                             = eCDF_AgentState_NotReady,
    .pcThingName                        = { 0 },
    .pMqttConnection                    = NULL,
    .xStartCertRotateSemaphore          = NULL,
    .xStatistics                        = { 0 },
    .xCdfApi                            = CDF_JOB_API_DEFAULT_INITIALIZER,
    .xOTACustomJobCallback              = prvCDFDefaultCustomJobCallback
};

OTA_JobParseErr_t prvCDF_CertRotateCallback( const char * pcJSON, uint32_t ulMsgLen )
{
    OTA_JobParseErr_t xReturn = eOTA_JobParseErr_None;
    int cert_rotation = false;
    const uint32_t batchSize=90;
    char tempBuffer[batchSize+1];
    tempBuffer[batchSize] = '\0';
    uint32_t printedLen = 0;
    int strFindRc;
    char *pcJSON_null;
    
    IotLogInfo( "prvCDF_CertRotateCallback called *************. ");

    if ( pcJSON != NULL )
    {
        /* Null terminate the JSON. */
        IotLogInfo( "msg len %d ", ulMsgLen);
        pcJSON_null = (char *) pcJSON + ulMsgLen;
        *pcJSON_null = '\0';

#ifdef DEBUG_CUSTOM_JOB_JSON
        while (printedLen < ulMsgLen)
        {
            memcpy(tempBuffer, pcJSON+printedLen, batchSize);
            IotLogInfo("%s", tempBuffer);
            printedLen += batchSize;
        }
        IotClock_SleepMs( 500 );
#endif
    }

    if (xCDF_Agent.eState == eCDF_AgentState_Ready)
    {
        if ( pcJSON != NULL )
        {
            debug_something = true;
            if (parseJsonCdfJob((char *) pcJSON))
            {
                IotLogInfo("prvCDF_CertRotateCallback: JSON parsing found CDF custom job");
                cert_rotation = true;
            }
            debug_something = false;
        }
    }
    else
    {
        IotLogInfo("prvCDF_CertRotateCallback: xCDF_Agent.eState != eCDF_AgentState_Ready");
    }
    

    if (cert_rotation)
    {
        if (newCertInProgress == false)
        {
            newCertInProgress = true;

            xSemaphoreGive(xCDF_Agent.xStartCertRotateSemaphore);
            IotLogInfo( "prvCDF_CertRotateCallback give start get cert.");
        }
        else
        {
            IotLogInfo( "prvCDF_CertRotateCallback: second attempt to gen cert before first completed");
        }
    }
    else
    {
        xReturn = (*xCDF_Agent.xOTACustomJobCallback)(pcJSON, ulMsgLen) ;      
    }
    return xReturn;
}

void CDF_AgentInit_internal( void * pMqttConnection,
                           const uint8_t * pcThingName,
                           OTA_PAL_Callbacks_t * otaCallbacks,
                           cdf_Api_t * xCdfApi,
                           TickType_t xTicksToWait )
{
    static TaskHandle_t pxCDF_TaskHandle;
    int i;

    BaseType_t xReturn = pdTRUE;

    if ( xCdfApi != NULL )
    {
        if( xCdfApi->xWriteCDFStateNVM != NULL )
        {
            xCDF_Agent.xCdfApi.xWriteCDFStateNVM = xCdfApi->xWriteCDFStateNVM;
        }
        if( xCdfApi->xReadCDFStateNVM != NULL )
        {
            xCDF_Agent.xCdfApi.xReadCDFStateNVM= xCdfApi->xReadCDFStateNVM;
        }
        if( xCdfApi->xPutTempDeviceCert != NULL )
        {
            xCDF_Agent.xCdfApi.xPutTempDeviceCert = xCdfApi->xPutTempDeviceCert;
        }
        if( xCdfApi->xGetTempDeviceCert != NULL )
        {
            xCDF_Agent.xCdfApi.xGetTempDeviceCert = xCdfApi->xGetTempDeviceCert;
        }
        if( xCdfApi->xPutDeviceCert != NULL )
        {
            xCDF_Agent.xCdfApi.xPutDeviceCert = xCdfApi->xPutDeviceCert;
        }
        if( xCdfApi->xGetDeviceCert != NULL )
        {
            xCDF_Agent.xCdfApi.xGetDeviceCert = xCdfApi->xGetDeviceCert;
        }
        if( xCdfApi->xPutDevicePrivateKey != NULL )
        {
            xCDF_Agent.xCdfApi.xPutDevicePrivateKey = xCdfApi->xPutDevicePrivateKey;
        }
        if( xCdfApi->xGetDevicePrivateKey != NULL )
        {
            xCDF_Agent.xCdfApi.xGetDevicePrivateKey = xCdfApi->xGetDevicePrivateKey;
        }
        if( xCdfApi->xPutCSR != NULL )
        {
            xCDF_Agent.xCdfApi.xPutCSR = xCdfApi->xPutCSR;
        }
        if( xCdfApi->xGetCSR != NULL )
        {
            xCDF_Agent.xCdfApi.xGetCSR = xCdfApi->xGetCSR;
        }
        if( xCdfApi->xRegisterDevice!= NULL )
        {
            xCDF_Agent.xCdfApi.xRegisterDevice = xCdfApi->xRegisterDevice;
        }
        if( xCdfApi->xGetNewCertificateId != NULL){
            xCDF_Agent.xCdfApi.xGetNewCertificateId = xCdfApi->xGetNewCertificateId;
        }
        if( xCdfApi->xPutNewCertificateId != NULL){
                    xCDF_Agent.xCdfApi.xPutNewCertificateId = xCdfApi->xPutNewCertificateId;
        }
        if( xCdfApi->xGetOldCertificateId != NULL){
                    xCDF_Agent.xCdfApi.xGetOldCertificateId = xCdfApi->xGetOldCertificateId;
        }
        if( xCdfApi->xPutOldCertificateId != NULL){
                            xCDF_Agent.xCdfApi.xPutOldCertificateId = xCdfApi->xPutOldCertificateId;
        }
    }

    /* Reset our statistics counters. */
    xCDF_Agent.xStatistics.ulCDF_PacketsReceived = 0;
    xCDF_Agent.xStatistics.ulCDF_PacketsDropped = 0;
    xCDF_Agent.xStatistics.ulCDF_PacketsQueued = 0;
    xCDF_Agent.xStatistics.ulCDF_PacketsProcessed = 0;
    xCDF_Agent.xStatistics.ulCDF_PublishFailures = 0;

    if( pcThingName != NULL )
    {
        uint32_t ulStrLen = strlen( ( const char * ) pcThingName );

        if( ulStrLen <= cdfconfigMAX_THINGNAME_LEN )
        {
            /* Store the Thing name to be used for topics later. */
            memcpy( xCDF_Agent.pcThingName, pcThingName, ulStrLen + 1UL ); /* Include zero terminator when saving the Thing name. */
        }
        else
        {
            IotLogError( "Thing name is too long.");
            xReturn = pdFALSE;
        }

        if( xReturn == pdTRUE)
        {
            xCDF_Agent.pMqttConnection = pMqttConnection;             
            xCDF_Agent.xStartCertRotateSemaphore = xSemaphoreCreateBinary();
            xCDF_Agent.eState = eCDF_AgentState_NotReady;

            if( xCDF_Agent.xStartCertRotateSemaphore == NULL )
            {
                IotLogError( "Semaphore not created");
                xReturn = pdFALSE;
            }

            if (xReturn == pdTRUE)
            {
                if (xTaskCreate( prvCDF_RotateCertTask, "CDF Rotate Cert Task", cdfconfigSTACK_SIZE, NULL,
                                 cdfconfigAGENT_PRIORITY, &pxCDF_TaskHandle )  != pdPASS)
                {
                    IotLogError( "CDF Task Did not start");
                    xReturn = pdFALSE;
                }
                else
                {
                    IotLogInfo( "CDF Task Started");
                }

                 if ( (xCdfApi->xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ||
                         (xCdfApi->xReadCDFStateNVM)() == CDF_STATE_ACK_CERT_ROTATE ||
                         (xCdfApi->xReadCDFStateNVM)() == CDF_STATE_DEACTIVATE_CERT)
                 {
                     /*
                     * Save the OTA custom job call back.
                     * Replace the OTA custom job call back with the CDF custom job callback.
                     * Then call the OTA custom job callback from witin the CDF custom job callback
                     */
                     xCDF_Agent.xOTACustomJobCallback = otaCallbacks->xCustomJobCallback;
                     otaCallbacks->xCustomJobCallback = prvCDF_CertRotateCallback;

                     /* Setup OTA and give it a second to start. */
                     OTA_AgentInit_internal( pMqttConnection,  pcThingName,
                         otaCallbacks,  xTicksToWait );
                 }

            } 

            if( xReturn == pdTRUE )
            {
                xReturn = pdFALSE;
                for (i = 0; i < _MAX_WAIT_CDF_TASK_ATTEMPTS; i++)
                {
                    if ( xCDF_Agent.eState != eCDF_AgentState_NotReady ) 
                    {
                        xReturn = pdTRUE;
                        break;
                    }
                    IotLogInfo( "Waiting for CDF Task to Start %d", i);
                    IotClock_SleepMs( _SYNC_WAIT_MS );
                }
            }

            if (xReturn != pdTRUE)
            {
                IotLogError( "cdf init failed %d", xReturn);
            }
        }
    }
    else
    {
        IotLogError( "thingName is null: cdf init failed ");
    }
    return ;
}

extern const char *pcOTAStateStr[];
/* Public API to shutdown the CDF and OTA Agents. */

void CDF_AgentShutdown( void )
{
    OTA_State_t eOTAState;
    IotLogInfo( "CDF_AgentShutdown: task is gone, clean up resources");
    vSemaphoreDelete(xCDF_Agent.xStartCertRotateSemaphore);
    /* Manish what should TickType value be. */
    if ( (xCDF_Agent.xCdfApi.xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ) 
    {
        IotLogInfo( "CDF_AgentShutdown: shut down OTA agent");
        OTA_AgentShutdown( (TickType_t) 20 );
        while (  ( eOTAState = OTA_GetAgentState() ) != eOTA_AgentState_NotReady ) 
        {
            IotClock_SleepMs( _SYNC_WAIT_MS );
            configPRINTF( ( "Shutting down OTA:  State: %s\r\n", pcOTAStateStr[eOTAState]) );
        }
    }
    return;
}


/* Return the current state of the OTA agent. */

CDF_State_t CDF_GetAgentState( void )
{
    return xCDF_Agent.eState;
}

uint32_t CDF_GetPacketsDropped( void )
{
    return xCDF_Agent.xStatistics.ulCDF_PacketsDropped;
}

uint32_t CDF_GetPacketsQueued( void )
{
    return xCDF_Agent.xStatistics.ulCDF_PacketsQueued;
}

uint32_t CDF_GetPacketsProcessed( void )
{
    return xCDF_Agent.xStatistics.ulCDF_PacketsProcessed;
}

uint32_t CDF_GetPacketsReceived( void )
{
    return xCDF_Agent.xStatistics.ulCDF_PacketsReceived;
}

int strFind(char * buffer, char * match)
{
    char *bufBeg, *bufEnd;
    char *matchBeg, *matchEnd;
    int char_cnt = -1;

    if (buffer && match)
    {
        bufBeg = buffer;
        bufEnd = buffer + strlen(buffer);
        matchEnd = match + strlen(match);
        while (bufBeg < bufEnd)
        {
            matchBeg = match;
            while (matchBeg < matchEnd)
            {
                if (*matchBeg++ != *bufBeg++)
                {
                    break;
                }
            }
            if (matchBeg == matchEnd)
            {
                char_cnt = (int) (bufBeg - buffer);
                break;
            }
        }
    }
    return char_cnt;
}

IotMqttError_t processPayload(
        char *payload, 
        CDF_CR_ACTION * cdfCrAction, 
        cdf_Api_t * cdfApi)
{
    int payloadStrLen, certDataLen;
    IotMqttError_t pubStatus = IOT_MQTT_BAD_PARAMETER;
    char certificate[1300];
    char newCertificateId[_CERTIFICATE_ID_LENGTH];
    char oldCertificateId[_CERTIFICATE_ID_LENGTH];


    IotLogInfo( "Process Payload ");
    IotClock_SleepMs( 1000 );

    if (payload)
    {
        payloadStrLen = strlen(payload);

        switch (*cdfCrAction)
        {
            case CDF_CR_GET_CERT:
                IotLogInfo( "processPayload CDF_CR_GET_CERT");
                IotLogInfo( "Sub Beg Payload: %1612s\r\n", payload );
                IotClock_SleepMs( 1000 );
#ifdef SAVE
                IotLogInfo( "Sub End 1 Payload: %100s\r\n", (payload + payloadStrLen - 300));
                IotLogInfo( "Sub End 2 Payload: %100s\r\n", (payload + payloadStrLen - 200));
                IotLogInfo( "Sub End 3 Payload: %100s\r\n", (payload + payloadStrLen - 100));
#endif
                /* Get the new certificate PEM from payload*/
                char * certStr = strstr(payload, _CR_GET_RESPONSE_STR_BEG);
                char * newCertIdStr = strstr(payload, _CR_GET_NEW_CERT_ID_STR_BEG);
                char * oldCertIdStr = strstr(payload, _CR_GET_OLD_CERT_ID_STR_BEG);


                if(certStr != NULL && certStr[0] == _CR_GET_RESPONSE_STR_BEG[0]){
                    certStr = (certStr + _CR_GET_RESPONSE_BEG_OFFSET);
                    certDataLen = strFind(certStr, _CR_GET_RESPONSE_STR_END);
                    if (certDataLen >= 0)
                   {
                       /* Null terminate and bypass charaters1 before the actual cert */
                       *(certStr + certDataLen + 1) = '\0';

                       strncpy(certificate, certStr, certDataLen + 1);

                       /* store the cert */
                       if (cdfApi->xPutTempDeviceCert(certificate) == EXIT_SUCCESS)
                       {
                           IotLogError( "IOT_MQTT_SUCCESS Have a temp cert %50s", certificate);
                       }
                       else
                       {
                           IotLogError( "New Cert was not stored properly");
                           break;
                       }
                   }
                    else{
                        IotLogError( "New Cert wasn't found");
                        break;
                    }

                }
                else{
                    IotLogError("couldn't find new cert pem");
                    break;
                }

                /* Get the new certificate Id from payload */
                if(newCertIdStr != NULL){
                    newCertIdStr = (newCertIdStr + strlen(_CR_GET_NEW_CERT_ID_STR_BEG));
                    newCertIdStr[_CERTIFICATE_ID_LENGTH - 1] = '\0';
                    strncpy(newCertificateId, newCertIdStr, _CERTIFICATE_ID_LENGTH);
                    if(cdfApi->xPutNewCertificateId(newCertificateId) == EXIT_SUCCESS){
                        IotLogError( "IOT_MQTT_SUCCESS Have a new cert id %50s", cdfApi->xGetNewCertificateId());
                    } 
                    else{
                        IotLogError( "IOT_MQTT_ERROR Couldn't put new certificate ID");
                    }
                    
                }
                else{
                    IotLogError( "New Cert Id was not found");
                    break;
                }
                /* Get the old certificate Id. Since we reuse the cdfAPI context we don't need
                to worry about losing this when we switch certificates */
                if(oldCertIdStr != NULL){
                    oldCertIdStr = (oldCertIdStr + strlen(_CR_GET_OLD_CERT_ID_STR_BEG));
                    oldCertIdStr[_CERTIFICATE_ID_LENGTH - 1] = '\0';
                    strncpy(oldCertificateId, oldCertIdStr, _CERTIFICATE_ID_LENGTH);
                    if(cdfApi->xPutOldCertificateId(oldCertificateId) == EXIT_SUCCESS){
                        IotLogError( "IOT_MQTT_SUCCESS Have an old cert id %50s", cdfApi->xGetOldCertificateId());
                        pubStatus = IOT_MQTT_SUCCESS;
                    }
                    else{
                        IotLogError( "IOT_MQTT_ERROR Couldn't put old certificate ID");
                    }
                   
                }
                else{
                    IotLogError( "Old Cert Id was not found");
                }
                break;
            case CDF_CR_ACK_CERT:
                IotLogInfo( "processPayload CDF_CR_ACK_CERT");
                IotLogInfo( "Sub Payload: %s", payload );
                IotClock_SleepMs( 1000 );
                /* match payload */
                /* check that payload is success */
                if (strncmp(payload, _CR_ACK_RESPONSE_STR_ERROR, _CR_ACK_RESPONSE_LEN)  != 0)
                {
                    pubStatus = IOT_MQTT_SUCCESS;
                }
                else
                {
                    IotLogError( "Ack response is not correct. Expected = %s, Actual = %s",
                        _CR_ACK_RESPONSE_STR_ERROR,
                        payload);
                }
                break;
            case CDF_CR_DEACTIVATE_CERT:
                IotLogInfo("processPayload CDF_CR_DEACTIVATE_CERT");
                IotLogInfo( "Sub Payload: %s", payload );
                IotClock_SleepMs( 1000 );
                if (strncmp(payload, _CR_ACK_RESPONSE_STR_ERROR, _CR_ACK_RESPONSE_LEN)  != 0)
                {
                    pubStatus = IOT_MQTT_SUCCESS;
                }
                else
                {
                    IotLogError( "Deactivate response is not correct. Expected = %s, Actual = %s",
                        _CR_ACK_RESPONSE_STR_ERROR,
                        payload);
                }
                break;
        }
    }
    return (pubStatus); 
} /*-----------------------------------------------------------*/ 
/**
 * @brief Called by the MQTT library when an incoming PUBLISH message is received.
 *
 * The demo uses this callback to handle incoming PUBLISH messages. This callback
 * prints the contents of an incoming message and publishes an acknowledgement
 * to the MQTT server.
 * @param[in] param1 Counts the total number of received PUBLISH messages. This
 * callback will increment this counter.
 * @param[in] pPublish Information about the incoming PUBLISH message passed by
 * the MQTT library.
 */
static void _mqttSubscriptionCallback( void * param1,
                                       IotMqttCallbackParam_t * const pPublish )
{
    int acknowledgementLength = 0;
    int payload_len = pPublish->u.message.info.payloadLength;    
    cdf_subCallbackParams_t * subCallbackParams;
    CDF_CR_ACTION * cdfCrAction;
    cdf_Api_t * cdfApi;
    IotSemaphore_t * pPublishesReceived;
    char * pPayload = (char *) pPublish->u.message.info.pPayload;
    char pAcknowledgementMessage[ _ACKNOWLEDGEMENT_MESSAGE_BUFFER_LENGTH ] = { 0 };
    IotMqttPublishInfo_t acknowledgementInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    IotMqttError_t pubStatus;

    subCallbackParams = (cdf_subCallbackParams_t *) param1;
    pPublishesReceived = subCallbackParams->pPublishesReceived;
    cdfCrAction = subCallbackParams->cdfCrAction;
    cdfApi = subCallbackParams->cdfApi;

    /* Print information about the incoming PUBLISH message. */
    IotLogInfo( "Incoming PUBLISH received:\n"
                "Subscription topic filter: %s\n"
                "Publish topic name: %s\n",
                pPublish->u.message.pTopicFilter,
                pPublish->u.message.info.pTopicName);

    IotLogInfo( "Sub Payload Len: %d", payload_len);
    IotLogInfo( "Mqtt Step: %d", *cdfCrAction );

    /* Set the members of the publish info for the acknowledgement message. */
    acknowledgementInfo.qos = IOT_MQTT_QOS_1;
    acknowledgementInfo.pTopicName = _ACKNOWLEDGEMENT_TOPIC_NAME;
    acknowledgementInfo.topicNameLength = _ACKNOWLEDGEMENT_TOPIC_NAME_LENGTH;
    acknowledgementInfo.pPayload = pAcknowledgementMessage;
    acknowledgementInfo.payloadLength = ( size_t ) acknowledgementLength;
    acknowledgementInfo.retryMs = _PUBLISH_RETRY_MS;
    acknowledgementInfo.retryLimit = _PUBLISH_RETRY_LIMIT;

    pubStatus = IotMqtt_TimedPublish( pPublish->mqttConnection,
                         &acknowledgementInfo,
                         0,
                         _MQTT_TIMEOUT_MS);

    if ( pubStatus == IOT_MQTT_SUCCESS )
    {
        *(pPayload + payload_len) = '\0';
        pubStatus = processPayload(pPayload, cdfCrAction, cdfApi);
        /* IotLogInfo( "Acknowledgment message for PUBLISH sent."); */
        if ( pubStatus == IOT_MQTT_SUCCESS )
        {
            /* Increment the number of PUBLISH messages received. */
            IotSemaphore_Post( pPublishesReceived );
        }
    }
    else
    {
        IotLogWarn( "Acknowledgment message for PUBLISH %s will NOT be sent.",
                         IotMqtt_strerror( pubStatus ) );
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief Add or remove subscriptions by either subscribing or unsubscribing.
 *
 * @param[in] mqttConnection The MQTT connection to use for subscriptions.
 * @param[in] operation Either #IOT_MQTT_SUBSCRIBE or #IOT_MQTT_UNSUBSCRIBE.
 * @param[in] pTopicFilters Array of topic filters for subscriptions.
 * @param[in] pCallbackParameter The parameter to pass to the subscription
 * callback.
 *
 * @return `EXIT_SUCCESS` if the subscription operation succeeded; `EXIT_FAILURE`
 * otherwise.
 */
static int _modifySubscriptions( IotMqttConnection_t mqttConnection,
                                 IotMqttOperationType_t operation,
                                 const char ** pTopicFilters,
                                 void * pCallbackParameter)
{
    int status = EXIT_SUCCESS;
    int32_t i = 0;
    IotMqttError_t subscriptionStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttSubscription_t pSubscriptions[ _CR_SUB_TOPIC_COUNT ] = { IOT_MQTT_SUBSCRIPTION_INITIALIZER };

    /* Set the members of the subscription list. */
    for( i = 0; i < _CR_SUB_TOPIC_COUNT; i++ )
    {
        pSubscriptions[ i ].qos = IOT_MQTT_QOS_1;
        pSubscriptions[ i ].pTopicFilter = pTopicFilters[ i ];
        /* TBD: question why original code had a len - 1, look for this if there is a problem */
        pSubscriptions[ i ].topicFilterLength = strlen( pSubscriptions[ i ].pTopicFilter );
        pSubscriptions[ i ].callback.pCallbackContext = pCallbackParameter;
        pSubscriptions[ i ].callback.function = _mqttSubscriptionCallback;
        IotLogInfo( "Subscribe filter %s.", pSubscriptions[ i ].pTopicFilter ); 
    }

    /* Modify subscriptions by either subscribing or unsubscribing. */
    if( operation == IOT_MQTT_SUBSCRIBE )
    {
        IotLogInfo( "before IotMqtt_TimedSubscribe() %d %d", _MQTT_TIMEOUT_MS, IOT_DEMO_MQTT_PUBLISH_BURST_SIZE);
        subscriptionStatus = IotMqtt_TimedSubscribe( mqttConnection,
                                                     pSubscriptions,
                                                     _CR_SUB_TOPIC_COUNT,
                                                     0,
                                                     _MQTT_TIMEOUT_MS );

        IotLogInfo( "after IotMqtt_TimedSubscribe() ");
        /* Check the status of SUBSCRIBE. */
        switch( subscriptionStatus )
        {
            case IOT_MQTT_SUCCESS:
                IotLogInfo( "All demo topic filter subscriptions accepted." ); 
                break;

            case IOT_MQTT_SERVER_REFUSED:

                /* Check which subscriptions were rejected before exiting the demo. */
                for( i = 0; i < _CR_SUB_TOPIC_COUNT; i++ )
                {
                    if( IotMqtt_IsSubscribed( mqttConnection,
                                              pSubscriptions[ i ].pTopicFilter,
                                              pSubscriptions[ i ].topicFilterLength,
                                              NULL ) == true )
                    {
                        /* IotLogInfo( "Topic filter %.*s was accepted.",
                         *           pSubscriptions[ i ].topicFilterLength,
                         *           pSubscriptions[ i ].pTopicFilter );
                         */
                    }
                    else
                    {
                        IotLogError( "Topic filter %.*s was rejected.",
                                     pSubscriptions[ i ].topicFilterLength,
                                     pSubscriptions[ i ].pTopicFilter );
                    }
                }

                status = EXIT_FAILURE;
                break;

            default:

                status = EXIT_FAILURE;
                break;
        }
    }
    else if( operation == IOT_MQTT_UNSUBSCRIBE )
    {
        subscriptionStatus = IotMqtt_TimedUnsubscribe( mqttConnection,
                                                       pSubscriptions,
                                                       _CR_SUB_TOPIC_COUNT,
                                                       0,
                                                       _MQTT_TIMEOUT_MS );

        /* Check the status of UNSUBSCRIBE. */
        if( subscriptionStatus != IOT_MQTT_SUCCESS )
        {
            status = EXIT_FAILURE;
        }
    }
    else
    {
        /* Only SUBSCRIBE and UNSUBSCRIBE are valid for modifying subscriptions. */
        IotLogError( "MQTT operation %s is not valid for modifying subscriptions.",
                     IotMqtt_OperationType( operation ) );

        status = EXIT_FAILURE;
    }

    return status;
}

/*-----------------------------------------------------------*/

/**
 * @brief Transmit all messages and wait for them to be received on topic filters.
 *
 * @param[in] mqttConnection The MQTT connection to use for publishing.
 * @param[in] pTopicNames Array of topic names for publishing. These were previously
 * subscribed to as topic filters.
 * @param[in] pPublishReceivedCounter Counts the number of messages received on
 * topic filters.
 *
 * @return `EXIT_SUCCESS` if all messages are published and received; `EXIT_FAILURE`
 * otherwise.
 */
static int _publishAllMessages( IotMqttConnection_t mqttConnection,
                                cdf_subCallbackParams_t * subCallbackParams,
                                const char ** pubTopics)
{
    int status = EXIT_SUCCESS;
    int pubTopicLen = 0, pubPayloadLen = 0;
    CDF_CR_ACTION *cdfCrAction;
    cdf_Api_t *cdfApi;
    IotSemaphore_t * pPublishesReceived;
    IotMqttError_t publishStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttPublishInfo_t publishInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    char pPublishPayload[ _PUBLISH_PAYLOAD_BUFFER_LENGTH ] = { 0 };
    int i, str_len;

    cdfCrAction = subCallbackParams->cdfCrAction;
    pPublishesReceived = subCallbackParams->pPublishesReceived;
    cdfApi = subCallbackParams->cdfApi;

    /* Set the common members of the publish info. */
    publishInfo.qos = IOT_MQTT_QOS_1;
    publishInfo.retryMs = _PUBLISH_RETRY_MS;
    publishInfo.pTopicName = (*pubTopics);
    publishInfo.pPayload = pPublishPayload;
    publishInfo.retryLimit = _PUBLISH_RETRY_LIMIT;

    /* Generate an publish topic */
    pubTopicLen = strlen ((*pubTopics)) ;
    publishInfo.topicNameLength = pubTopicLen;

    /* Generate the payload for the PUBLISH. */
    if (*cdfCrAction == CDF_CR_GET_CERT)
    {
        IotLogInfo( "CR ACTION GET CERT");
        pubPayloadLen = snprintf( pPublishPayload, _PUBLISH_PAYLOAD_BUFFER_LENGTH,
                           "{\"csr\": \"%s\"}",
                           cdfApi->xGetCSR());
    }
    else if (*cdfCrAction == CDF_CR_DEACTIVATE_CERT){
        pubPayloadLen = snprintf( pPublishPayload, _PUBLISH_PAYLOAD_BUFFER_LENGTH,
                           "{\"oldCertificateId\": \"%s\"}",
            cdfApi->xGetOldCertificateId());
    }
    else if (*cdfCrAction == CDF_CR_ACK_CERT){
        IotLogInfo( "CR ACTION ACK CERT");
        pubPayloadLen = snprintf( pPublishPayload, _PUBLISH_PAYLOAD_BUFFER_LENGTH,
                           "{\"newCertificateId\": \"%s\"}",
            cdfApi->xGetNewCertificateId());
    }
    else
    {
        IotLogError( "_publishAllMessages: MQTT Step ERROR");
    }

    /* Check for errors in loading the payload */
    if( pubPayloadLen <= 0 )
    {
        IotLogError( "_publishAllMessages: Failed to generate MQTT PUBLISH payload for PUBLISH ");
        status = EXIT_FAILURE;
    }
    else
    {
        publishInfo.payloadLength = ( size_t ) pubPayloadLen;
        
        IotLogInfo( "before IoTMqtt_TimedPublish");
        /* PUBLISH a message. This is an asynchronous function that notifies of
         * completion through a callback. */
        publishStatus = IotMqtt_TimedPublish( mqttConnection,
                                         &publishInfo,
                                         0,
                                         _MQTT_TIMEOUT_MS);

        if( publishStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "_publishAllMessages: MQTT PUBLISH returned error %s.",
                         IotMqtt_strerror( publishStatus ) );
            status = EXIT_FAILURE;
        }
        /* Wait on the semaphonre twice as long as the pub timeout */
        else if( IotSemaphore_TimedWait( pPublishesReceived,
                                    (_MQTT_TIMEOUT_MS * 2) ) == false )
        {
            IotLogError( "_publishAllMessages: Timed out waiting for incoming PUBLISH messages." );

            status = EXIT_FAILURE;
        }
    }
    return status;
}


int cdf_CertRotateAction(
    IotMqttConnection_t mqttConnection,
    const char ** subTopics,
    const char ** pubTopics,
    cdf_Api_t * cdfApi,
    CDF_CR_ACTION * cdfCrAction)
{
    int status;
    int mqtt_publish_attempts;
    cdf_subCallbackParams_t subCallbackParams; 

    /* Counts the number of incoming PUBLISHES received (and allows the demo
     * application to wait on incoming PUBLISH messages). */
    IotSemaphore_t publishesReceived; 

    /* Store data in struct used in subscritption callback */
    subCallbackParams.pPublishesReceived = &publishesReceived;
    subCallbackParams.cdfCrAction = cdfCrAction;
    subCallbackParams.cdfApi = cdfApi;

    IotLogError( "cdf_CertRotateAction: enter");

    /* Add the topic filter subscriptions used in this demo. */
    status = _modifySubscriptions( mqttConnection,
                                   IOT_MQTT_SUBSCRIBE,
                                   subTopics,
                                   &subCallbackParams);
    if (status != EXIT_SUCCESS )
    {
        IotLogError( "cdf_CertRotateAction: Failed to subscribe topics");
    }
    else
    {

        IotLogError( "create semaphore");
        /* Create the semaphore to count incoming PUBLISH messages. */
        if( IotSemaphore_Create( &publishesReceived,
                                 0,
                                 IOT_DEMO_MQTT_PUBLISH_BURST_SIZE ) == true )
        {
            status = EXIT_FAILURE;

            for (mqtt_publish_attempts = 0; 
                 mqtt_publish_attempts < _MAX_MQTT_PUBLISH_ATTEMPTS &&
                 status != EXIT_SUCCESS; 
                 mqtt_publish_attempts++)
            {
                IotLogError( "publishAllMessage: attempt = %d", mqtt_publish_attempts);

                /* PUBLISH (and wait) for all messages. */
                status = _publishAllMessages( mqttConnection,
                                              &subCallbackParams,
                                              pubTopics);

            }
            /* Destroy the incoming PUBLISH counter. */
            IotSemaphore_Destroy( &publishesReceived );
        }
        else
        {
            /* Failed to create incoming PUBLISH counter. */
            status = EXIT_FAILURE;
        }

        /* Remove the topic subscription filters used in this demo. */
        if (_modifySubscriptions( mqttConnection,
                                       IOT_MQTT_UNSUBSCRIBE,
                                       subTopics,
                                       NULL) != EXIT_SUCCESS)
        {
            status = EXIT_FAILURE;
        }

    }
    return (status);
}

int cdf_GetNewCert( IotMqttConnection_t mqttConnection )
{
    int status = EXIT_FAILURE;
    int mqtt_get_cert_attempts;
    CDF_CR_ACTION cdfCrAction; 
    cdf_Api_t *cdfApi;

    cdfApi = &(xCDF_Agent.xCdfApi);

    /* Subscribe Topics to Get a Certificate */
    const char * subGetTopics[ _CR_SUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/result/" clientcredentialIOT_THING_NAME,
    };
    const char * pubGetTopics[ _CR_PUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/attach/" clientcredentialIOT_THING_NAME,
    };

    /* Subscribe Topics to Acknowledge a Certificate */
    const char * subAckTopics[ _CR_SUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/result/" clientcredentialIOT_THING_NAME,
    };
    const char * pubAckTopics[ _CR_PUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/activate/" clientcredentialIOT_THING_NAME,
    };

     /* Subscribe Topics to Deactivate a Certificate */
    const char * subDeactivateTopics[ _CR_SUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/result/" clientcredentialIOT_THING_NAME,
    };
    const char * pubDeactivateTopics[ _CR_PUB_TOPIC_COUNT ] =
    {
       _CR_TOPIC_PREFIX "/detach/" clientcredentialIOT_THING_NAME,
    };


    for (mqtt_get_cert_attempts = 0; 
         mqtt_get_cert_attempts < _MAX_MQTT_GET_CERT_ATTEMPTS &&
         status != EXIT_SUCCESS; 
         mqtt_get_cert_attempts++)
    {

        /* Mark the MQTT connection as established. */
        if (xCDF_Agent.eState == eCDF_AgentState_GetCert)
        {
            IotLogInfo( "cdf_GetNewCert: CDF_CR_GET_CERT: attempt = %d", mqtt_get_cert_attempts);
            cdfCrAction = CDF_CR_GET_CERT;

            /* Get the new cert */
            status =  cdf_CertRotateAction(
                        mqttConnection,
                        subGetTopics,
                        pubGetTopics,
                        cdfApi,
                        &cdfCrAction);
        }
        else if (xCDF_Agent.eState == eCDF_AgentState_AckCert)
        {
            IotLogInfo( "cdf_GetNewCert: CDF_CR_ACK_CERT: attempt = %d", mqtt_get_cert_attempts);
            /* Acknowledge the new cert */
            cdfCrAction = CDF_CR_ACK_CERT;
            status =  cdf_CertRotateAction(
                        mqttConnection,
                        subAckTopics,
                        pubAckTopics,
                        cdfApi,
                        &cdfCrAction);
        }
        else if (xCDF_Agent.eState == eCDF_AgentState_DeactivateCert)
        {
            IotLogInfo( "cdf_GetNewCert: CDF_CR_DEACTIVATE_CERT: attempt = %d", mqtt_get_cert_attempts);
            /* Deactivate the old cert */
            cdfCrAction = CDF_CR_DEACTIVATE_CERT;
            status = cdf_CertRotateAction(
                        mqttConnection,
                        subDeactivateTopics,
                        pubDeactivateTopics,
                        cdfApi,
                        &cdfCrAction);
        }
        else
        {
            IotLogInfo( "cdf_CertRotateAction in agent state %d Failed", xCDF_Agent.eState);
        }
    }
    return status;
}


/* NOTE: Task to process CDF Rotate Cert requests. */

static void prvCDF_RotateCertTask( void * pvUnused )
{
    static TaskHandle_t handle;
    BaseType_t taskRet;

    IotLogInfo( "prvCDF_RotateCertTask Started");
    newCertInProgress = false;

    /* Put the CDF agent in the ready state. */
    xCDF_Agent.eState = eCDF_AgentState_Ready;
    if ( (xCDF_Agent.xCdfApi.xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ) 
    {
        // xSemaphoreTake(xCDF_Agent.xStartCertRotateSemaphore, portMAX_DELAY); 

        xCDF_Agent.eState = eCDF_AgentState_GetCert;
        IotLogInfo( "CDF Get Cert GET");

        if (cdf_GetNewCert( xCDF_Agent.pMqttConnection ) == EXIT_SUCCESS)
        {
            xCDF_Agent.eState = eCDF_AgentState_AckCert;
        }
        else
        {
            xCDF_Agent.eState = eCDF_AgentState_NotReady;
        }
    }
    else if (( (xCDF_Agent.xCdfApi.xReadCDFStateNVM)() == CDF_STATE_ACK_CERT_ROTATE ))
    {
        xCDF_Agent.eState = eCDF_AgentState_AckCert;
        IotLogInfo( "CDF Get Cert ACK");

        if (cdf_GetNewCert( xCDF_Agent.pMqttConnection ) == EXIT_SUCCESS)
        {
            xCDF_Agent.eState = eCDF_AgentState_DeactivateCert;
        }
        else
        {
            xCDF_Agent.eState = eCDF_AgentState_NotReady;
        }
    }
    else
    {
        xCDF_Agent.eState = eCDF_AgentState_DeactivateCert;
        IotLogInfo( "CDF Get Cert DEACTIVATE");

        if (cdf_GetNewCert( xCDF_Agent.pMqttConnection ) == EXIT_SUCCESS)
        {
            xCDF_Agent.eState = eCDF_AgentState_ShuttingDown;
        }
        else
        {
            xCDF_Agent.eState = eCDF_AgentState_NotReady;
        }
    }

    vTaskDelete( NULL );
}
