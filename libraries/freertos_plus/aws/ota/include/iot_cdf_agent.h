/*
 * Amazon FreeRTOS
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

/**
 * @file aws_ota_agent.h
 * @brief OTA Agent Interface
 */

#ifndef _IOT_CDF_AGENT_H_
#define _IOT_CDF_AGENT_H_

/* Includes required by the FreeRTOS timers structure. */
#include "FreeRTOS.h"
#include "timers.h"

/* Include for console serial output. */
#include "iot_logging_task.h"

#define MAC_ADDR "CC50E388186C"

#define _CERTIFICATE_ID_LENGTH ( 65 )

/**
 * @brief CDF Agent states.
 *
 * The current state of the OTA Task (OTA Agent).
 *
 * @note There is currently support only for a single OTA context.
 */
typedef enum
{
    eCDF_AgentState_NotReady = 0,     /*!< The CDF agent task is not running. */
    eCDF_AgentState_Ready = 1,        /*!< The CDF agent task is running and ready for CDF command . */
    eCDF_AgentState_GetCert = 2,       /*!< The CDF agent is processing Get Cert. */
    eCDF_AgentState_AckCert = 3,       /*!< The CDF agent is processing Ack Cert. */
    eCDF_AgentState_ShuttingDown = 4, /*!< The CDF agent task is performing shut down activities. */
    eCDF_NumAgentStates = 5,
    eCDF_AgentState_DeactivateCert = 6 /* The CDF agent is ready to deactivate the old certificate */
} CDF_State_t;

/*
 * States to identify if rotating out the factory 
 * cert or a cert from the cloud.  This is used in the case
 * of a different set of actions are taken during 
 * the appliance registration process with a cert that has limited
 * privileges vs a cert rotation of a fully privileged 
 * cert from the cloud.
 */
typedef enum 
{
    CDF_STATE_UNKNOWN,           
    CDF_STATE_JITR,  
    CDF_STATE_APP_REG,        
    CDF_STATE_WAIT_FOR_CERT_ROTATE, 
    CDF_STATE_ACK_CERT_ROTATE,
    CDF_STATE_DEACTIVATE_CERT,
    CDF_STATE_FINISHED      
} CDF_STATE;

typedef enum 
{
    CDF_CR_GET_CERT,       /* Get Factory Cert */
    CDF_CR_ACK_CERT,       /* Ack Facotry Cert */
    CDF_CR_DEACTIVATE_CERT,  /* Deactivate Factory Cert*/
} CDF_CR_ACTION;

/* 
 * Store a null terminated string
 */
typedef uint8_t (*pxCDF_Put)(char *);
/* 
 * Get a null terminated string
 */
typedef char (* (*pxCDF_Get)( void ));
/* 
 * Write value to fixed location in NV memmory
 */
typedef uint8_t (*pxCDF_StateWrite)(CDF_STATE val);
/* 
 * Read value from fixed location in NV memmory
 */
typedef CDF_STATE (*pxCDF_StateRead)( void );
/* 
 * Perform some CDF action
 */
typedef int (*pxCDF_Act)( void );

typedef struct _cdf_Api {
    pxCDF_StateWrite  xWriteCDFStateNVM;
    pxCDF_StateRead   xReadCDFStateNVM;
    pxCDF_Put         xPutTempDeviceCert;
    pxCDF_Get         xGetTempDeviceCert;
    pxCDF_Put         xPutDeviceCert;
    pxCDF_Get         xGetDeviceCert;
    pxCDF_Put         xPutDevicePrivateKey;
    pxCDF_Get         xGetDevicePrivateKey;
    pxCDF_Put         xPutCSR;
    pxCDF_Get         xGetCSR;
    pxCDF_Act         xRegisterDevice;
    pxCDF_Put         xPutNewCertificateId;
    pxCDF_Get         xGetNewCertificateId;
    pxCDF_Put         xPutOldCertificateId;
    pxCDF_Get         xGetOldCertificateId;
} cdf_Api_t ;

typedef struct _subCallbackParamsStruct {
    IotSemaphore_t * pPublishesReceived;
    cdf_Api_t * cdfApi;
    CDF_CR_ACTION * cdfCrAction;
} cdf_subCallbackParams_t;

typedef struct _subAppRegCallbackParamsStruct {
    IotSemaphore_t * pPublishesReceived;
} cdf_subAppRegCallbackParams_t;

extern int mbed_getCSR( char *csr_str, char *private_key_str, char *certificate_str );

/*---------------------------------------------------------------------------*/
/*								Public API									 */
/*---------------------------------------------------------------------------*/

/**
 * @brief OTA Agent initialization function.
 *
 * Initialize the OTA engine by starting the OTA Agent ("OTA Task") in the system. This function must
 * be called with the MQTT messaging client context before calling OTA_CheckForUpdate(). Only one
 * OTA Agent may exist.
 *
 * @param[in] pvClient The messaging protocol client context (e.g. an MQTT context).
 * @param[in] pucThingName A pointer to a C string holding the Thing name.
 * @param[in] xFunc Static callback function for when an OTA job is complete. This function will have
 * input of the state of the OTA image after download and during self-test.
 * @param[in] xTicksToWait The number of ticks to wait until the OTA Task signals that it is ready.
 * If this is set to zero, then the function will return immediately after creating the OTA task but
 * the OTA task may not be ready to operate yet. The state may be queried with OTA_GetAgentState().
 *
 * @return The state of the OTA Agent upon return from the OTA_State_t enum.
 * If the agent was successfully initialized and ready to operate, the state will be
 * eOTA_AgentState_Ready. Otherwise, it will be one of the other OTA_State_t enum values.
 */
void  CDF_AgentInit_internal( void * pMqttConnection,
                           const uint8_t * pcThingName,
                           OTA_PAL_Callbacks_t * otaCallbacks,
                           cdf_Api_t * xApi,
                           TickType_t xTicksToWait );

/**
 * @brief Signal to the CDF Agent to shut down.
 *
 * Signals the OTA agent task to shut down. The OTA agent will unsubscribe from all MQTT job
 * notification topics, stop in progress OTA jobs, if any, and clear all resources.
 *
 * @param[in] xTicksToWait The number of ticks to wait for the OTA Agent to complete the shutdown process.
 * If this is set to zero, the function will return immediately without waiting. The actual state is
 * returned to the caller.
 *
 * @return One of the OTA agent states from the OTA_State_t enum.
 * A normal shutdown will return eOTA_AgentState_NotReady. Otherwise, refer to the OTA_State_t enum for details.
 */
void CDF_AgentShutdown( void ) ;

/**
 * @brief Get the current state of the OTA agent.
 *
 * @return The current state of the OTA agent.
 */
CDF_State_t CDF_GetAgentState( void );


/*---------------------------------------------------------------------------*/
/*							Statistics API									 */
/*---------------------------------------------------------------------------*/

/**
 * @brief Get the number of CDF message packets received by the CDF agent.
 *
 * @note Calling CDF_AgentInit() will reset this statistic.
 *
 * @return The number of CDF packets that have been received but not
 * necessarily queued for processing by the CDF agent.
 */
uint32_t CDF_GetPacketsReceived( void );

/**
 * @brief Get the number of CDF message packets queued by the CDF agent.
 *
 * @note Calling CDF_AgentInit() will reset this statistic.
 *
 * @return The number of CDF packets that have been queued for processing.
 * This implies there was a free message queue entry so it can be passed
 * to the agent for processing.
 */
uint32_t CDF_GetPacketsQueued( void );

/**
 * @brief Get the number of CDF message packets processed by the CDF agent.
 *
 * @note Calling CDF_AgentInit() will reset this statistic.
 *
 * @return the number of CDF packets that have actually been processed.
 *
 */
uint32_t CDF_GetPacketsProcessed( void );

/**
 * @brief The OTA agent task priority. Normally it runs at a low priority.
 */
#define cdfconfigAGENT_PRIORITY                 tskIDLE_PRIORITY 
/**
 * @brief Get the number of CDF message packets dropped by the CDF agent.
 *
 * @note Calling CDF_AgentInit() will reset this statistic.
 *
 * @return the number of CDF packets that have been dropped because
 * of either no queue or at shutdown cleanup.
 */
uint32_t CDF_GetPacketsDropped( void );

/*
 * PEM-encoded Cert Signing Request (CSR)
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----\n"
 */
#define keyCLIENT_CSR_PEM \
"-----BEGIN CERTIFICATE REQUEST-----\n"\
"MIICrzCCAZcCAQAwPDELMAkGA1UEBhMCQ0gxEjAQBgNVBAoMCUJyZXYgRGVtbzEZ\n"\
"MBcGA1UEAwwQd3d3LmJyZXZEZW1vLm9yZzCCASIwDQYJKoZIhvcNAQEBBQADggEP\n"\
"ADCCAQoCggEBAL4UsIKCPGxARYqyJsN81Ji7bFK414kDILHWBUL5WylfCxRYFSdd\n"\
"gaWguZ/zi/SDrs9oM80J8AibZryMYqZBdyEobIzz+5TMHmWJkD6KcLsqpB7MTT2v\n"\
"lGxyYjsYJGB61fx9/uWDLu/LhKk9qaCBnGZN8EGV5A7r8RgJoO1N7eB1IHqwcDQq\n"\
"1BpMFLqSnExHzpJn7ZdDP/Tur0GAwiiBmf2IBVm3zTsxEC5Eum3ynK9BUaFq22Xs\n"\
"9EPL52MVNd9xwRv1BL0vUy0qDWh2PjojvD2g1+hlQzOIPLaT/vsGh4ovqExjXo+c\n"\
"tO6955D9QBwYxGkpMdSPX4gBWGQw/5++IsUCAwEAAaAuMCwGCSqGSIb3DQEJDjEf\n"\
"MB0wGwYDVR0RBBQwEoIQd3d3LmJyZXZEZW1vLm9yZzANBgkqhkiG9w0BAQsFAAOC\n"\
"AQEARjfHW+t961bvnAWBweY8Xr2XJkhHvhy5Cjt0ln208fqoytoSd8NBRI+4XUxk\n"\
"c4isx9WmEZUYBgGoEuHa0ls9Eksgs7gQaANgv6sxst9d+3IcY1MxGI/6jNPdcRR1\n"\
"qAuTqSG6G+OUSg7qgxk6U7iBBdx9D9rUI8e5l16nQFa0O0bCafdgI3GKc1/wxG/r\n"\
"dc48pnxglxapINg8hNe/GwVCxzcevqs3ISr7i+7OGf2xwLOTCQICmrENCkIb/xZL\n"\
"IwhMVvpNHqsPjFkQF7Gafppx6PCkDwZugtUGH1E222YG49GsUmh1pSRG9QmcT5En\n"\
"A0LZ+m74ELLZnQV/O/S7RAMbpQ==\n"\
"-----END CERTIFICATE REQUEST-----\n"

#define cdfconfigMAX_THINGNAME_LEN              64U
#define cdfconfigSTACK_SIZE                     6000U
#define cdfSmallSTACK_SIZE                      700U

#define _CR_CERTIFICATE_SIZE     ( sizeof( keyCLIENT_CERTIFICATE_PEM ) + 500U )
#define _CR_CSR_SIZE             ( sizeof( keyCLIENT_CSR_PEM ) + 500U )
#define _CR_PRIVATE_KEY_SIZE     ( sizeof( keyCLIENT_PRIVATE_KEY_PEM ) + 500U )

#define DEBUG_CUSTOM_JOB_JSON
#define DEBUG_CERT_ROTATE_JSON
#define DEBUG_APPL_REG_JSON
// #define DEBUG_CSR_AND_CERT

#endif /* ifndef _IOT_CDF_AGENT_H_ */
