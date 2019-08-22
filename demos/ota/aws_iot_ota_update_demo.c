/*
Amazon FreeRTOS OTA Update Demo V1.4.4
Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 http://aws.amazon.com/freertos
 http://www.FreeRTOS.org
*/


/**
 * @file aws_ota_update_demo.c
 * @brief A simple OTA update example.
 *
 * This example initializes the OTA agent to enable OTA updates via the
 * MQTT broker. It simply connects to the MQTT broker with the users
 * credentials and spins in an indefinite loop to allow MQTT messages to be
 * forwarded to the OTA agent for possible processing. The OTA agent does all
 * of the real work; checking to see if the message topic is one destined for
 * the OTA agent. If not, it is simply ignored.
 */
/* The config header is always included first. */
#include "iot_config.h"

/* MQTT include. */
#include "iot_mqtt.h"
/* Standard includes. */
#include <stdio.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* Demo network handling */
#include "aws_iot_demo_network.h"

/* Required to get the broker address and port. */
#include "aws_clientcredential.h"
#include "aws_clientcredential_keys.h"

/* Amazon FreeRTOS OTA agent includes. */
#include "aws_iot_ota_agent.h"
#include "aws_ota_pal.h"

#include "iot_network_manager_private.h"

/* Required for demo task stack and priority */
#include "aws_demo_config.h"
#include "aws_application_version.h"

/* Required for CDF */
#include "aws_iot_ota_agent.h"
#include "iot_cdf_agent.h"
#include "aws_dev_mode_key_provisioning.h"

static void App_OTACompleteCallback(OTA_JobEvent_t eEvent );

void OTAMqttEchoDemoNetworkDisconnectedCallback( const IotNetworkInterface_t *pNetworkInterface );
void OTAMqttEchoDemoNetworkConnectedCallback ( bool awsIotMqttMode,
                                    const char * pIdentifier,
                                    void * pNetworkServerInfo,
                                    void * pNetworkCredentialInfo,
                                    const IotNetworkInterface_t * pNetworkInterface );

extern int vRunCDF_OTADemo ( bool awsIotMqttMode,
                 const char * pIdentifier,
                 void * pNetworkServerInfo,
                 void * pNetworkCredentialInfo,
                 const IotNetworkInterface_t * pNetworkInterface );

/*-----------------------------------------------------------*/

#define otaDemoCONN_TIMEOUT_MS              ( 10000UL )

#define otaDemoCONN_RETRY_INTERVAL_MS       ( 5000 )

#define otaDemoCONN_RETRY_LIMIT             ( 100 )

#define demoCONN_RETRY_LIMIT             ( 5 )

#define otaDemoKEEPALIVE_SECONDS            ( 1200 )

#define myappONE_SECOND_DELAY_IN_TICKS  pdMS_TO_TICKS( 1000UL )

#define otaDemoNETWORK_TYPES               ( AWSIOT_NETWORK_TYPE_ALL ) 
/**
 * @cond DOXYGEN_IGNORE
 * Doxygen should ignore this section.
 *
 * Provide default values for undefined configuration settings.
 */
    #define IOT_DEMO_MQTT_TOPIC_PREFIX           "iotdemo"

/**
 * @brief The first characters in the client identifier. A timestamp is appended
 * to this prefix to create a unique client identifer.
 *
 * This prefix is also used to generate topic names and topic filters used in this
 * demo.
 */
#define CLIENT_IDENTIFIER_PREFIX                 "iotdemo"
/**
 * @brief The longest client identifier that an MQTT server must accept (as defined
 * by the MQTT 3.1.1 spec) is 23 characters. Add 1 to include the length of the NULL
 * terminator.
 */
#define CLIENT_IDENTIFIER_MAX_LENGTH             ( 24 )

/**
 * @brief The keep-alive interval used for this demo.
 *
 * An MQTT ping request will be sent periodically at this interval.
 */
#define KEEP_ALIVE_SECONDS                       ( 60 )

/**
 * @brief The timeout for MQTT operations in this demo.
 */
#define MQTT_TIMEOUT_MS                          ( 5000 )

/**
 * @brief The Last Will and Testament topic name in this demo.
 *
 * The MQTT server will publish a message to this topic name if this client is
 * unexpectedly disconnected.
 */
#define WILL_TOPIC_NAME                          IOT_DEMO_MQTT_TOPIC_PREFIX "/will"

/**
 * @brief The length of #WILL_TOPIC_NAME.
 */
#define WILL_TOPIC_NAME_LENGTH                   ( ( uint16_t ) ( sizeof( WILL_TOPIC_NAME ) - 1 ) )

/**
 * @brief The message to publish to #WILL_TOPIC_NAME.
 */
#define WILL_MESSAGE                             "MQTT demo unexpectedly disconnected."

/**
 * @brief The length of #WILL_MESSAGE.
 */
#define WILL_MESSAGE_LENGTH                      ( ( size_t ) ( sizeof( WILL_MESSAGE ) - 1 ) )
/**
 * @brief Structure which holds the context for an MQTT connection within Demo.
 */
/**
 * @brief Flag used to unset, during disconnection of currently connected network. This will
 * trigger a reconnection from the main MQTT task.
 */
BaseType_t xNetworkConnected = pdFALSE;

void OTAMqttEchoDemoNetworkDisconnectedCallback( const IotNetworkInterface_t *pNetworkInterface )
{
    IotLogError( "OTAMqttEchoDemoNetworkDisconnectedCallback: ." );
    xNetworkConnected = pdFALSE;
}
/* */

void OTAMqttEchoDemoNetworkConnectedCallback ( bool awsIotMqttMode,
                                    const char * pIdentifier,
                                    void * pNetworkServerInfo,
                                    void * pNetworkCredentialInfo,
                                    const IotNetworkInterface_t * pNetworkInterface )
{
    IotLogError( "OTAMqttEchoDemoNetworkConnectedCallback: ." );
    xNetworkConnected = pdTRUE;
} 

/**
 * @brief Establish a new connection to the MQTT server.
 *
 * @param[in] awsIotMqttMode Specify if this demo is running with the AWS IoT
 * MQTT server. Set this to `false` if using another MQTT server.
 * @param[in] pIdentifier NULL-terminated MQTT client identifier.
 * @param[in] pNetworkServerInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkCredentialInfo Passed to the MQTT connect function when
 * establishing the MQTT connection.
 * @param[in] pNetworkInterface The network interface to use for this demo.
 * @param[out] pMqttConnection Set to the handle to the new MQTT connection.
 *
 * @return `EXIT_SUCCESS` if the connection is successfully established; `EXIT_FAILURE`
 * otherwise.
 */
int _establishMqttConnection( bool awsIotMqttMode,
                                     bool useLWT,
                                     const char * pIdentifier,
                                     void * pNetworkServerInfo,
                                     void * pNetworkCredentialInfo,
                                     const IotNetworkInterface_t * pNetworkInterface,
                                     IotMqttConnection_t * pMqttConnection )
{
    int status = EXIT_SUCCESS;
    IotMqttError_t connectStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttNetworkInfo_t networkInfo = IOT_MQTT_NETWORK_INFO_INITIALIZER;
    IotMqttConnectInfo_t connectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER;
    IotMqttPublishInfo_t willInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    char pClientIdentifierBuffer[ CLIENT_IDENTIFIER_MAX_LENGTH ] = { 0 };

    /* Set the members of the network info not set by the initializer. This
     * struct provided information on the transport layer to the MQTT connection. */
    networkInfo.createNetworkConnection = true;
    networkInfo.u.setup.pNetworkServerInfo = pNetworkServerInfo;
    networkInfo.u.setup.pNetworkCredentialInfo = pNetworkCredentialInfo;
    networkInfo.pNetworkInterface = pNetworkInterface;

    #if ( IOT_MQTT_ENABLE_SERIALIZER_OVERRIDES == 1 ) && defined( IOT_DEMO_MQTT_SERIALIZER )
        networkInfo.pSerializer = IOT_DEMO_MQTT_SERIALIZER;
    #endif

    /* Set the members of the connection info not set by the initializer. */
    connectInfo.awsIotMqttMode = awsIotMqttMode;
    connectInfo.cleanSession = true;
    connectInfo.keepAliveSeconds = KEEP_ALIVE_SECONDS;
    if (useLWT)
    {
        connectInfo.pWillInfo = &willInfo;
    }
    else
    {
        connectInfo.pWillInfo = NULL;
    }

    /* Set the members of the Last Will and Testament (LWT) message info. The
     * MQTT server will publish the LWT message if this client disconnects
     * unexpectedly. */
    willInfo.pTopicName = WILL_TOPIC_NAME;
    willInfo.topicNameLength = WILL_TOPIC_NAME_LENGTH;
    willInfo.pPayload = WILL_MESSAGE;
    willInfo.payloadLength = WILL_MESSAGE_LENGTH;

    /* Use the parameter client identifier if provided. Otherwise, generate a
     * unique client identifier. */
    if( pIdentifier != NULL )
    {
        connectInfo.pClientIdentifier = pIdentifier;
        connectInfo.clientIdentifierLength = ( uint16_t ) strlen( pIdentifier );
    }
    else
    {
        /* Every active MQTT connection must have a unique client identifier. The demos
         * generate this unique client identifier by appending a timestamp to a common
         * prefix. */
        status = snprintf( pClientIdentifierBuffer,
                           CLIENT_IDENTIFIER_MAX_LENGTH,
                           CLIENT_IDENTIFIER_PREFIX "%lu",
                           ( long unsigned int ) IotClock_GetTimeMs() );

        /* Check for errors from snprintf. */
        if( status < 0 )
        {
            IotLogError( "Failed to generate unique client identifier for demo." );
            status = EXIT_FAILURE;
        }
        else
        {
            /* Set the client identifier buffer and length. */
            connectInfo.pClientIdentifier = pClientIdentifierBuffer;
            connectInfo.clientIdentifierLength = ( uint16_t ) status;

            status = EXIT_SUCCESS;
        }
    }

    /* Establish the MQTT connection. */
    if( status == EXIT_SUCCESS )
    {
        IotLogInfo( "MQTT demo client identifier is %.*s (length %hu).",
                    connectInfo.clientIdentifierLength,
                    connectInfo.pClientIdentifier,
                    connectInfo.clientIdentifierLength );

        connectStatus = IotMqtt_Connect( &networkInfo,
                                         &connectInfo,
                                         MQTT_TIMEOUT_MS,
                                         pMqttConnection );

        if( connectStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "MQTT CONNECT returned error %s.",
                         IotMqtt_strerror( connectStatus ) );

            status = EXIT_FAILURE;
        }
    }

    return status;
}

const char *pcCDFStateStr[eCDF_NumAgentStates] =
{
     "Not Ready",
     "Ready",
     "Get Cert",
     "Ack Cert",
     "Shutting down"
};

const char *pcOTAStateStr[eOTA_NumAgentStates] =
{
     "Not Ready",
     "Ready",
     "Active",
     "Shutting down"
};

static OTA_PAL_ImageState_t CurrentImageState = eOTA_PAL_ImageState_Valid;

OTA_JobParseErr_t otaDemoCustomJobCallback( const char * pcJSON, uint32_t ulMsgLen )
{
    DEFINE_OTA_METHOD_NAME( "prvDefaultCustomJobCallback" );
    const uint32_t batchSize=90;
    char tempBuffer[batchSize+1];
    tempBuffer[batchSize] = '\0';
    uint32_t printedLen = 0;
    configPRINTF(("Job Found:\r\n"));
    if ( pcJSON != NULL )
    {
        while (printedLen < ulMsgLen)
        {
            memcpy(tempBuffer, pcJSON+printedLen, batchSize);
            configPRINTF(("%s", tempBuffer));
            printedLen += batchSize;
        }
    }

    OTA_LOG_L1( "[%s] Received Custom Job inside OTA Demo.\r\n", OTA_METHOD_NAME );

    return eOTA_JobParseErr_None;
}


OTA_Err_t prvPAL_Abort_customer( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_Abort_customer" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_AbortFailed;
    }

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_Abort( C );
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor\r\n", OTA_METHOD_NAME );
        return kOTA_Err_None;
    }
}



OTA_Err_t prvPAL_ActivateNewImage_customer( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ActivateNewImage_customer" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_ActivateNewImage();
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        // Reset self after doing cleanup
        return prvPAL_ActivateNewImage();
    }
}


OTA_Err_t prvPAL_CloseFile_customer( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile_customer" );

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_CloseFile( C );
    }
    else
    {
        OTA_LOG_L1( "[%s] Received prvPAL_CloseFile_customer inside OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        C->pucFile = (uint8_t *)0;
        return kOTA_Err_None;
    }
}

OTA_Err_t prvPAL_CreateFileForRx_customer( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CreateFileForRx_customer" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return kOTA_Err_RxFileCreateFailed;
    }

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_CreateFileForRx( C );
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );

        // Put a value in the file handle
        C->pucFile = (uint8_t *)C;

        return kOTA_Err_None;
    }    
}

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_customer( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState_customer" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_GetPlatformImageState();
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        return CurrentImageState;
    }    
}

OTA_Err_t prvPAL_ResetDevice_customer( uint32_t ulServerFileID )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ResetDevice_customer" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_ResetDevice();
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        return kOTA_Err_None;
    }    
}

OTA_Err_t prvPAL_SetPlatformImageState_customer( uint32_t ulServerFileID, OTA_ImageState_t eState )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_SetPlatformImageState_customer" );

    if ( ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_SetPlatformImageState(eState);
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        
        if ( eState==eOTA_ImageState_Testing )
        {
            CurrentImageState = eOTA_PAL_ImageState_PendingCommit;
        }

        return kOTA_Err_None;
    }    
}

int16_t prvPAL_WriteBlock_customer( OTA_FileContext_t * const C,
                           uint32_t iOffset,
                           uint8_t * const pacData,
                           uint32_t iBlockSize )
 {
    DEFINE_OTA_METHOD_NAME( "prvPAL_WriteBlock_customer" );

    if ( C == NULL )
    {
        OTA_LOG_L1( "[%s] File context null\r\n", OTA_METHOD_NAME );
        return -1;
    }    

    if ( C->ulServerFileID == 0 )
    {
        // Update self
        return prvPAL_WriteBlock(C, iOffset, pacData, iBlockSize);
    }
    else
    {
        OTA_LOG_L1( "[%s] OTA Demo for secondary processor.\r\n", OTA_METHOD_NAME );
        return (int16_t) iBlockSize;
    }    
}                          

int vRunOTAUpdateDemo( cdf_Api_t *cdfApi,
                bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface )
{
    int status = EXIT_SUCCESS;
    int mqtt_connect_attempts;
    OTA_State_t eOTAState;
    CDF_State_t eCDFState;
    /* Handle of the MQTT connection used in this demo. */
    IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;
    /*  IotMqttConnectInfo_t xConnectInfo = IOT_MQTT_CONNECT_INFO_INITIALIZER; */

    OTA_PAL_Callbacks_t otaCallbacks = { \
        .xAbort                    = prvPAL_Abort_customer, \
        .xActivateNewImage         = prvPAL_ActivateNewImage_customer,\
        .xCloseFile                = prvPAL_CloseFile_customer, \
        .xCreateFileForRx          = prvPAL_CreateFileForRx_customer, \
        .xGetPlatformImageState    = prvPAL_GetPlatformImageState_customer, \
        .xResetDevice              = prvPAL_ResetDevice_customer, \
        .xSetPlatformImageState    = prvPAL_SetPlatformImageState_customer, \
        .xWriteBlock               = prvPAL_WriteBlock_customer, \
        .xCompleteCallback         = App_OTACompleteCallback, \
        .xCustomJobCallback        = otaDemoCustomJobCallback \
    };

    if ( (cdfApi->xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ) 
    {
        configPRINTF ( ("OTA demo version %u.%u.%u\r\n",
            xAppFirmwareVersion.u.x.ucMajor,
            xAppFirmwareVersion.u.x.ucMinor,
            xAppFirmwareVersion.u.x.usBuild ) );
    }

    configPRINTF( ( "Creating MQTT Client...\r\n" ) );
    vTaskDelay( myappONE_SECOND_DELAY_IN_TICKS );
    if( IotMqtt_Init() != IOT_MQTT_SUCCESS )
    {
        /* Failed to initialize MQTT library. */
        status = EXIT_FAILURE;
        configPRINTF( ( "IotMqtt_Init() not okay \r\n" ) );
        vTaskDelay( myappONE_SECOND_DELAY_IN_TICKS );
    }
    else
    {
        configPRINTF( ( "IotMqtt_Init() okay \r\n" ) );
        vTaskDelay( myappONE_SECOND_DELAY_IN_TICKS );
    }

    /* Create the MQTT Client. */
    for (mqtt_connect_attempts = 0; mqtt_connect_attempts < demoCONN_RETRY_LIMIT;  )
    {
        status = EXIT_FAILURE;

        if( xNetworkConnected == pdTRUE )
        {
            configPRINTF( ( "Connecting to broker...\r\n" ) );

            /* Establish a new MQTT connection. */
            status = _establishMqttConnection( awsIotMqttMode,
                                               true,
                                               pIdentifier,
                                               pNetworkServerInfo,
                                               pNetworkCredentialInfo,
                                               pNetworkInterface,
                                               &mqttConnection );
            /* Connect to the broker. */
            if( status == EXIT_SUCCESS )
            {
                    CDF_AgentInit_internal( mqttConnection, ( const uint8_t * ) ( clientcredentialIOT_THING_NAME ), &otaCallbacks, cdfApi, ( TickType_t ) ~0 ); 

                    if ( (cdfApi->xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ) 
                    {

                        while ( (( eOTAState = OTA_GetAgentState() ) != eOTA_AgentState_NotReady ) &&  
                               ((( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_Ready ) || 
                                (( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_GetCert ) ) && 
                                (xNetworkConnected == pdTRUE) )
                        {
                            /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
                            configPRINTF( ( "CDF: State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcCDFStateStr[eCDFState],
                                    CDF_GetPacketsReceived(), CDF_GetPacketsQueued(), CDF_GetPacketsProcessed(), CDF_GetPacketsDropped() ) );
                            configPRINTF( ( "OTA:  State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcOTAStateStr[eOTAState], 
                                    OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped() ) ); 
                            vTaskDelay( 2 * myappONE_SECOND_DELAY_IN_TICKS );
                        }

                        if (CDF_GetAgentState() != eCDF_AgentState_AckCert ) 
                        {
                            /* Cert Rotation did not move to AckCert state */
                            /* Manish Verify states when OTA Agent is Not Ready. Is it a failure also? */
                            status = EXIT_FAILURE;
                        }
                    }
                    else if( ( (cdfApi->xReadCDFStateNVM)() == CDF_STATE_ACK_CERT_ROTATE )){
                        while ( (( eOTAState = OTA_GetAgentState() ) != eOTA_AgentState_NotReady ) &&  
                            ((( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_Ready ) || 
                            (( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_AckCert ) ) && 
                            (xNetworkConnected == pdTRUE) )
                        {
                            /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
                            configPRINTF( ( "CDF: State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcCDFStateStr[eCDFState],
                                    CDF_GetPacketsReceived(), CDF_GetPacketsQueued(), CDF_GetPacketsProcessed(), CDF_GetPacketsDropped() ) );
                            configPRINTF( ( "OTA:  State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcOTAStateStr[eOTAState], 
                                    OTA_GetPacketsReceived(), OTA_GetPacketsQueued(), OTA_GetPacketsProcessed(), OTA_GetPacketsDropped() ) ); 
                            vTaskDelay( 2 * myappONE_SECOND_DELAY_IN_TICKS );
                        }

                        if (CDF_GetAgentState() != eCDF_AgentState_DeactivateCert ) 
                        {
                            /* Cert Rotation did not move to DeactivateCert state */
                            /* Manish Verify states when OTA Agent is Not Ready. Is it a failure also? */
                            status = EXIT_FAILURE;
                        }
                    }
                    else
                    {
                        while ( ( (( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_Ready ) ||
                                  (( eCDFState = CDF_GetAgentState() ) == eCDF_AgentState_AckCert )  ||
                                  (eCDFState = CDF_GetAgentState()) == eCDF_AgentState_DeactivateCert) &&
                                  (xNetworkConnected == pdTRUE) )
                        {
                            /* Wait forever for OTA traffic but allow other tasks to run and output statistics only once per second. */
                            configPRINTF( ( "CDF: State: %s  Received: %u   Queued: %u   Processed: %u   Dropped: %u\r\n", pcCDFStateStr[eCDFState],
                                    CDF_GetPacketsReceived(), CDF_GetPacketsQueued(), CDF_GetPacketsProcessed(), CDF_GetPacketsDropped() ) );
                            vTaskDelay( 2 * myappONE_SECOND_DELAY_IN_TICKS );
                        }

                        if (CDF_GetAgentState() != eCDF_AgentState_ShuttingDown )
                        {
                            /* Cert Rotation did not move to AckCert state */
                            /* Manish Verify states when OTA Agent is Not Ready. Is it a failure also? */
                            status = EXIT_FAILURE;
                        }
                    }
                    
                    /* Shutdown CDF and OTA Agents */
                    CDF_AgentShutdown();
                    IotMqtt_Disconnect( mqttConnection, false);
                    break;
            }
            else
            {
                configPRINTF( ( "ERROR:  _establishMqttConnection() Failed.\r\n" ) );
                mqtt_connect_attempts++;
            }
        }
        else
        {
            configPRINTF( ( "Network not ready\r\n" ) );
        }
        vTaskDelay( 5 * myappONE_SECOND_DELAY_IN_TICKS );
    }
    IotMqtt_Cleanup();
    return (status);
}


/* The OTA agent has completed the update job or determined that we're in
 * self test mode. If it was accepted, we want to activate the new image.
 * This typically means we should reset the device to run the new firmware.
 * If now is not a good time to reset the device, it may be activated later
 * by your user code. If the update was rejected, just return without doing
 * anything and we'll wait for another job. If it reported that we should
 * start test mode, normally we would perform some kind of system checks to
 * make sure our new firmware does the basic things we think it should do
 * but we'll just go ahead and set the image as accepted for demo purposes.
 * The accept function varies depending on your platform. Refer to the OTA
 * PAL implementation for your platform in aws_ota_pal.c to see what it
 * does for you.
 */

static void App_OTACompleteCallback( OTA_JobEvent_t eEvent )
{
	OTA_Err_t xErr = kOTA_Err_Uninitialized;

    /* OTA job is completed. so delete the MQTT and network connection. */
    if ( eEvent == eOTA_JobEvent_Activate )
    {
        configPRINTF( ( "Received eOTA_JobEvent_Activate callback from OTA Agent.\r\n" ) );
        /*  IotMqtt_Disconnect( xConnection.xMqttConnection, 0 ); */
        /*  vMqttDemoDeleteNetworkConnection( &xConnection ); */
        OTA_ActivateNewImage();
    }
    else if (eEvent == eOTA_JobEvent_Fail)
    {
        configPRINTF( ( "Received eOTA_JobEvent_Fail callback from OTA Agent.\r\n" ) );
        /* Nothing special to do. The OTA agent handles it. */
    }
	else if (eEvent == eOTA_JobEvent_StartTest)
	{
		/* This demo just accepts the image since it was a good OTA update and networking
		 * and services are all working (or we wouldn't have made it this far). If this
		 * were some custom device that wants to test other things before calling it OK,
		 * this would be the place to kick off those tests before calling OTA_SetImageState()
		 * with the final result of either accepted or rejected. */
        configPRINTF( ( "Received eOTA_JobEvent_StartTest callback from OTA Agent.\r\n" ) );
	    xErr = OTA_SetImageState (eOTA_ImageState_Accepted);
        if( xErr != kOTA_Err_None )
        {
            OTA_LOG_L1( " Error! Failed to set image state as accepted.\r\n" );
        }
	}
}

/*-----------------------------------------------------------*/

int vStartOTAUpdateDemoTask( bool awsIotMqttMode,
                 const char * pIdentifier,
                 void * pNetworkServerInfo,
                 void * pNetworkCredentialInfo,
                 const IotNetworkInterface_t * pNetworkInterface )
{
    int xRet = EXIT_SUCCESS;

    configPRINTF(( "vStartOTAUpdateDemoTask Enter.\r\n" ));
    xRet = vRunCDF_OTADemo(awsIotMqttMode,
                 pIdentifier,
                 pNetworkServerInfo,
                 pNetworkCredentialInfo,
                 pNetworkInterface );
    return xRet;
}
