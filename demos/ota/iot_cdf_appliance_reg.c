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
#include "../../libraries/3rdparty/jsmn/jsmn.h"

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
#include "iot_cdf_agent.h"

#define _APP_REG_ONE_SECOND_DELAY_IN_TICKS     pdMS_TO_TICKS( 1000UL )
#define _APP_REG_CONN_RETRY_LIMIT              ( 10 )
#define _APP_REG_MAX_MQTT_ATTEMPTS             ( 5 )
#define _APP_REG_SUB_TOPIC_COUNT               ( 1 )
#define _APP_REG_PUB_TOPIC_COUNT               ( 1 )
#define _APP_REG_PAYLOAD_BUFFER_LENGTH         ( 200 )
#define _APP_REG_TOPIC_PREFIX                  "app/BMC800/"
#define _APP_REG_PUBLISH_RETRY_LIMIT           ( 10 )
#define _APP_REG_PUBLISH_RETRY_MS              ( 1000 )
#define _APP_REG_ACK_TOPIC_NAME                _APP_REG_TOPIC_PREFIX "/acknowledgements"
#define _APP_REG_ACK_TOPIC_NAME_LENGTH         ( ( uint16_t ) ( sizeof( _APP_REG_ACK_TOPIC_NAME ) ) )
#define _APP_REG_MQTT_TIMEOUT_MS               ( 5000 )
#define _MAX_JSON_APPL_KEY_LEN                 ( 25 ) 
#define _MAX_JSON_APPL_VAL_LEN                 ( 50 ) 
#define _APPL_REG_JSON_MATCH_COUNT             ( 4 )
#define _MAX_APPL_REG_TOKEN_COUNT              ( 10 )
#define _MAX_APPL_REG_JSON_STR                 ( 200 )

/**
 * 
 * @brief Flag used to unset, during disconnection of currently connected network. This will
 * trigger a reconnection from the main MQTT task.
 */
extern BaseType_t xNetworkConnected;

extern int _establishMqttConnection( bool awsIotMqttMode,
                                     bool useLWT,
                                     const char * pIdentifier,
                                     void * pNetworkServerInfo,
                                     void * pNetworkCredentialInfo,
                                     const IotNetworkInterface_t * pNetworkInterface,
                                     IotMqttConnection_t * pMqttConnection );

extern int cleanupJsonStr(char * json_str, int max_length);

int parseJsonApplianceReg(char *json_str)
{
    char key[_MAX_JSON_APPL_KEY_LEN];
    char value[_MAX_JSON_APPL_VAL_LEN];

    int i;
    int r;
    int status_val;
    int match_countdown = _APPL_REG_JSON_MATCH_COUNT;

    jsmn_parser p;
    jsmntok_t t[_MAX_APPL_REG_TOKEN_COUNT];

    jsmn_init(&p);

    r = jsmn_parse(&p, json_str, strlen(json_str), t, sizeof(t)/(sizeof(t[0])));

    if (r < 0)
    {
        IotLogError("parseJsonApplianceReg: JSON parsing failed. JSON = %s", json_str);
        return false;
    }

    /* Assume the top-level element is an object */
    if (r < 1 || t[0].type != JSMN_OBJECT)
    {
        IotLogError("parseJsonApplianceReg: JSON parsing expected Object");
        return false;
    }

    for (i = 1; i < r; i++)
    {
        jsmntok_t json_value = t[i+1];
        jsmntok_t json_key = t[i];

        int val_length = json_value.end - json_value.start;
        int key_length = json_key.end - json_key.start;

        int idx;

        for (idx = 0; idx < val_length && idx < (_MAX_JSON_APPL_VAL_LEN - 1); idx++)
        {
            value[idx] = json_str[json_value.start + idx ];
        }

        for (idx = 0; idx < key_length && idx < (_MAX_JSON_APPL_KEY_LEN - 1); idx++)
        {
            key[idx] = json_str[json_key.start + idx];
        }

        key[key_length] = '\0';
        value[val_length] = '\0';

#ifdef DEBUG_APPL_REG_JSON
        IotLogInfo("parseJsonApplianceReg: key %s", key);
        IotLogInfo("parseJsonApplianceReg: key len %d, max key len %d", key_length, _MAX_JSON_APPL_KEY_LEN);

        IotLogInfo("parseJsonApplianceReg: value %s", value);
        IotLogInfo("parseJsonApplianceReg: value len %d, max value len %d", val_length, _MAX_JSON_APPL_VAL_LEN);
        IotClock_SleepMs( 500 );
#endif
        /* 
         * Make sure the number of compares matches the 
         * _CDF_JOB_JSON_MATCH_COUNT and a match_countdown--
         * happends for each.
         */
        if (!strcmp(key, "status"))
        {   
            status_val = atoi(value);
            if (status_val < 200 || status_val > 299)
            {
                return(false);
            }
            match_countdown--;
        }
        if (!strcmp(key, "msgid"))
        {   
            match_countdown--;
        }
        if (!strcmp(key, "description"))
        {   
            match_countdown--;
        }
        if (!strcmp(key, "data"))
        {   
            match_countdown--;
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


IotMqttError_t processAppRegPayload( char *payload)
{
    IotMqttError_t pubStatus = IOT_MQTT_BAD_PARAMETER;

    IotLogInfo( "processAppRegPayload: Process AppReg Payload ");

    if (payload)
    {

#ifdef DEBUG_APPL_REG_JSON
        IotLogInfo( "JSON = %s<", payload);
#endif

        if (cleanupJsonStr(payload, _MAX_APPL_REG_JSON_STR))
        {
            if (parseJsonApplianceReg(payload))
            {
                pubStatus = IOT_MQTT_SUCCESS;
            }
            else
            {
                IotLogError("processAppRegPayload: JSON parse error.");
            }
        }
        else
        {
            IotLogError("processAppRegPayload: JSON not clean.");
        }
    }
    else
    {
        IotLogError("processAppRegPayload: payload is empty.");
    }
    return (pubStatus); 
} 

/*-----------------------------------------------------------*/ 
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
static void _mqttAppRegSubscriptionCallback( void * param1,
                                       IotMqttCallbackParam_t * const pPublish )
{
    int acknowledgementLength = 0;
    int payload_len = pPublish->u.message.info.payloadLength;
    cdf_subAppRegCallbackParams_t * subCallbackParams;
    IotSemaphore_t * pPublishesReceived;
    char * pPayload = (char *) pPublish->u.message.info.pPayload;
    char pAcknowledgementMessage[ _APP_REG_PAYLOAD_BUFFER_LENGTH ] = { 0 };
    IotMqttPublishInfo_t acknowledgementInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    IotMqttError_t pubStatus;

    subCallbackParams = (cdf_subAppRegCallbackParams_t *) param1;
    pPublishesReceived = subCallbackParams->pPublishesReceived;

    /* Print information about the incoming PUBLISH message. */
    IotLogInfo( "Whitllist Incoming PUBLISH received:\n"
                "Subscription topic filter: %s\n"
                "Publish topic name: %s\n",
                pPublish->u.message.pTopicFilter,
                pPublish->u.message.info.pTopicName);

    IotLogInfo( "Sub Payload Len: %d", payload_len);

    /* Set the members of the publish info for the acknowledgement message. */
    acknowledgementInfo.qos = IOT_MQTT_QOS_1;
    acknowledgementInfo.pTopicName = _APP_REG_ACK_TOPIC_NAME;
    acknowledgementInfo.topicNameLength = _APP_REG_ACK_TOPIC_NAME_LENGTH;
    acknowledgementInfo.pPayload = pAcknowledgementMessage;
    acknowledgementInfo.payloadLength = ( size_t ) acknowledgementLength;
    acknowledgementInfo.retryMs = _APP_REG_PUBLISH_RETRY_MS;
    acknowledgementInfo.retryLimit = _APP_REG_PUBLISH_RETRY_LIMIT;

    pubStatus = IotMqtt_TimedPublish( pPublish->mqttConnection,
                         &acknowledgementInfo,
                         0,
                         _APP_REG_MQTT_TIMEOUT_MS);

    if ( pubStatus != IOT_MQTT_SUCCESS )
    {
        IotLogWarn( "Acknowledgment message for PUBLISH %s will NOT be sent.",
                         IotMqtt_strerror( pubStatus ) );
    }
    if (pPayload)
    {
        *(pPayload + payload_len) = '\0';
        pubStatus = processAppRegPayload(pPayload);
        if ( pubStatus == IOT_MQTT_SUCCESS )
        {
            /* Increment the number of PUBLISH messages received. */
            IotSemaphore_Post( pPublishesReceived );
        }
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
static int _modifyAppRegSubscriptions( IotMqttConnection_t mqttConnection,
                                 IotMqttOperationType_t operation,
                                 const char ** pTopicFilters,
                                 void * pCallbackParameter)
{
    int status = EXIT_SUCCESS;
    int32_t i = 0;
    IotMqttError_t subscriptionStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttSubscription_t pSubscriptions[ _APP_REG_SUB_TOPIC_COUNT ] = { IOT_MQTT_SUBSCRIPTION_INITIALIZER };

    /* Set the members of the subscription list. */
    for( i = 0; i < _APP_REG_SUB_TOPIC_COUNT; i++ )
    {
        pSubscriptions[ i ].qos = IOT_MQTT_QOS_1;
        pSubscriptions[ i ].pTopicFilter = pTopicFilters[ i ];
        /* TBD: question why original code had a len - 1, look for this if there is a problem */
        pSubscriptions[ i ].topicFilterLength = strlen( pSubscriptions[ i ].pTopicFilter );
        pSubscriptions[ i ].callback.pCallbackContext = pCallbackParameter;
        pSubscriptions[ i ].callback.function = _mqttAppRegSubscriptionCallback;
        IotLogInfo( "Subscribe filter %s.", pSubscriptions[ i ].pTopicFilter ); 
    }

    /* Modify subscriptions by either subscribing or unsubscribing. */
    if( operation == IOT_MQTT_SUBSCRIBE )
    {
        IotLogInfo( "before IotMqtt_TimedSubscribe() %d %d", _APP_REG_MQTT_TIMEOUT_MS, IOT_DEMO_MQTT_PUBLISH_BURST_SIZE);
        subscriptionStatus = IotMqtt_TimedSubscribe( mqttConnection,
                                                     pSubscriptions,
                                                     _APP_REG_SUB_TOPIC_COUNT,
                                                     0,
                                                     _APP_REG_MQTT_TIMEOUT_MS);

        IotLogInfo( "after IotMqtt_TimedSubscribe() ");
        /* Check the status of SUBSCRIBE. */
        switch( subscriptionStatus )
        {
            case IOT_MQTT_SUCCESS:
                IotLogInfo( "All demo topic filter subscriptions accepted." ); 
                break;

            case IOT_MQTT_SERVER_REFUSED:

                /* Check which subscriptions were rejected before exiting the demo. */
                for( i = 0; i < _APP_REG_SUB_TOPIC_COUNT; i++ )
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
                                                       _APP_REG_SUB_TOPIC_COUNT,
                                                       0,
                                                       _APP_REG_MQTT_TIMEOUT_MS);

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

static int _publishAppRegMessages( IotMqttConnection_t mqttConnection,
                                cdf_subAppRegCallbackParams_t * subCallbackParams,
                                const char ** pubTopics)
{
    int status = EXIT_SUCCESS;
    int pubTopicLen = 0, pubPayloadLen = 0;
    IotSemaphore_t * pPublishesReceived;
    IotMqttError_t publishStatus = IOT_MQTT_STATUS_PENDING;
    IotMqttPublishInfo_t publishInfo = IOT_MQTT_PUBLISH_INFO_INITIALIZER;
    char pPublishPayload[ _APP_REG_PAYLOAD_BUFFER_LENGTH ] = { 0 };

    pPublishesReceived = subCallbackParams->pPublishesReceived;

    /* Set the common members of the publish info. */
    publishInfo.qos = IOT_MQTT_QOS_1;
    publishInfo.retryMs = _APP_REG_PUBLISH_RETRY_MS;
    publishInfo.pTopicName = (*pubTopics);
    publishInfo.pPayload = pPublishPayload;
    publishInfo.retryLimit = _APP_REG_PUBLISH_RETRY_LIMIT;

    /* Generate an publish topic */
    pubTopicLen = strlen ((*pubTopics)) ;
    publishInfo.topicNameLength = pubTopicLen;

    /* Generate the payload for the PUBLISH. */
    pubPayloadLen = snprintf( pPublishPayload, _APP_REG_PAYLOAD_BUFFER_LENGTH,
                    "{\"msgid\":\"27f94a76-06f6-41f2-be83-48fd79d689f2\",\"appliance_ts\":1561292159.554015,\"data\":{\"mac_address\":\"%s\"}}",
                        MAC_ADDR);

    /* Check for errors in loading the payload */
    if( pubPayloadLen <= 0 )
    {
        IotLogError( "_publishAppRegMessages: Failed to generate MQTT PUBLISH payload for PUBLISH ");
        status = EXIT_FAILURE;
    }
    else
    {
        publishInfo.payloadLength = ( size_t ) pubPayloadLen;
        
        IotLogInfo( "_publishAppRegMessages: payload Len %d", pubPayloadLen);
        IotLogInfo( "_publishAppRegMessages: payload first 100: %-100s.\n", pPublishPayload);
        if (pubPayloadLen > 100)
        {
            IotLogInfo( "_publishAppRegMessages: payload after 90: %-100s.\n", (pPublishPayload + 90));
        }
        vTaskDelay( _APP_REG_ONE_SECOND_DELAY_IN_TICKS );
        /* PUBLISH a message. This is an asynchronous function that notifies of
         * completion through a callback. */
        publishStatus = IotMqtt_TimedPublish( mqttConnection,
                                         &publishInfo,
                                         0,
                                         _APP_REG_MQTT_TIMEOUT_MS);

        if( publishStatus != IOT_MQTT_SUCCESS )
        {
            IotLogError( "_publishAppRegMessages: MQTT PUBLISH returned error %s.",
                         IotMqtt_strerror( publishStatus ) );
            status = EXIT_FAILURE;
        }
        /* Wait on the semaphonre twice as long as the pub timeout */
        else if( IotSemaphore_TimedWait( pPublishesReceived,
                                    (_APP_REG_MQTT_TIMEOUT_MS * 2) ) == false )
        {
            IotLogError( "_publishAppRegMessages: Timed out waiting for incoming PUBLISH messages." );

            status = EXIT_FAILURE;
        }
    }
    return status;
}

int cdf_MqttAppRegAction(
    IotMqttConnection_t mqttConnection,
    const char ** subTopics,
    const char ** pubTopics)
{
    int status;
    cdf_subAppRegCallbackParams_t subCallbackParams; 

    /* Counts the number of incoming PUBLISHES received (and allows the demo
     * application to wait on incoming PUBLISH messages). */
    IotSemaphore_t publishesReceived; 

    /* Store data in struct used in subscritption callback */
    subCallbackParams.pPublishesReceived = &publishesReceived;

    IotLogError( "cdf_MqttAppRegAction: enter");

    /* Add the topic filter subscriptions used in this demo. */
    status = _modifyAppRegSubscriptions( mqttConnection,
                                   IOT_MQTT_SUBSCRIBE,
                                   subTopics,
                                   &subCallbackParams);

    if (status != EXIT_SUCCESS )
    {
        IotLogError( "cdf_MqttAppRegAction: Failed to subscribe topics");
    }
    else
    {

        IotLogError( "cdf_MqttAppRegAction: create pub semaphore");
        /* Create the semaphore to count incoming PUBLISH messages. */
        if( IotSemaphore_Create( &publishesReceived,
                                 0,
                                 IOT_DEMO_MQTT_PUBLISH_BURST_SIZE ) == true )
        {
            status = EXIT_FAILURE;

            /* PUBLISH (and wait) for all messages. */
            status = _publishAppRegMessages( mqttConnection,
                                              &subCallbackParams,
                                              pubTopics);

            /* Destroy the incoming PUBLISH counter. */
            IotSemaphore_Destroy( &publishesReceived );
        }
        else
        {
            /* Failed to create incoming PUBLISH counter. */
            status = EXIT_FAILURE;
        }

        /* Remove the topic subscription filters used in this demo. */
        if (_modifyAppRegSubscriptions( mqttConnection,
                                       IOT_MQTT_UNSUBSCRIBE,
                                       subTopics,
                                       NULL) != EXIT_SUCCESS)
        {
            status = EXIT_FAILURE;
        }

    }
    return (status);
}

int cdf_AppReg( IotMqttConnection_t mqttConnection )
{
    int status = EXIT_FAILURE;
    int mqtt_AppReg_attempts;

    /* Subscribe Topics to Get a Certificate */
    const char * subGetTopics[ _APP_REG_SUB_TOPIC_COUNT ] =
    {
       _APP_REG_TOPIC_PREFIX clientcredentialIOT_THING_NAME "/appregister/response",
    };
    const char * pubGetTopics[ _APP_REG_PUB_TOPIC_COUNT ] =
    {
       _APP_REG_TOPIC_PREFIX clientcredentialIOT_THING_NAME "/appregister/request",
    };


    for (mqtt_AppReg_attempts = 0; 
         mqtt_AppReg_attempts < _APP_REG_MAX_MQTT_ATTEMPTS &&
         status != EXIT_SUCCESS; 
         mqtt_AppReg_attempts++)
    {

        status =  cdf_MqttAppRegAction(
                    mqttConnection,
                    subGetTopics,
                    pubGetTopics);

        if (status != EXIT_SUCCESS)
        {
            IotLogInfo( "cdf_AppReg mqtt pub/sub attempt number %d Failed", mqtt_AppReg_attempts);
        }
    }
    return status;
}

int vCdfAppReg(bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface )
{
    int status = EXIT_SUCCESS;
    int mqtt_connect_attempts;
    /* Handle of the MQTT connection used in this demo. */
    IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

    configPRINTF( ( "AppReging Creating MQTT Client...\r\n" ) );
    vTaskDelay( _APP_REG_ONE_SECOND_DELAY_IN_TICKS );
    if( IotMqtt_Init() != IOT_MQTT_SUCCESS )
    {
        /* Failed to initialize MQTT library. */
        status = EXIT_FAILURE;
        configPRINTF( ( "IotMqtt_Init() not okay \r\n" ) );
        vTaskDelay( _APP_REG_ONE_SECOND_DELAY_IN_TICKS );
    }
    else
    {
        configPRINTF( ( "IotMqtt_Init() okay \r\n" ) );
        vTaskDelay( _APP_REG_ONE_SECOND_DELAY_IN_TICKS );
    }

    /* Code does not get to this point unless the network is up and running */
    xNetworkConnected = pdTRUE;

    for (mqtt_connect_attempts = 0; mqtt_connect_attempts < _APP_REG_CONN_RETRY_LIMIT;  )
    {
        if( xNetworkConnected == pdTRUE )
        {
            configPRINTF( ( "AppReging Connecting to broker...\r\n" ) );

            /* 
             * Establish a new MQTT connection.
             */
            status = _establishMqttConnection( awsIotMqttMode,
                                               false, 
                                               pIdentifier,
                                               pNetworkServerInfo,
                                               pNetworkCredentialInfo,
                                               pNetworkInterface,
                                               &mqttConnection );
            /* Connect to the broker. */
            if( status == EXIT_SUCCESS )
            {
                status = cdf_AppReg(mqttConnection );
                if (status == EXIT_SUCCESS)
                { 
                    IotMqtt_Disconnect( mqttConnection, false);
                    break;
                }
            }
            else 
            {
                mqtt_connect_attempts++;
            }
        }
        else
        {
            configPRINTF( ( "Network not ready\r\n" ) );
        }

        vTaskDelay( 5 * _APP_REG_ONE_SECOND_DELAY_IN_TICKS );
    }
    IotMqtt_Cleanup();
    return (status);
}

