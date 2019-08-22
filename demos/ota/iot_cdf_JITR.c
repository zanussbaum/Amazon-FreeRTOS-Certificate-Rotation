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

#define jitrONE_SECOND_DELAY_IN_TICKS  pdMS_TO_TICKS( 1000UL )
#define jitrCONN_RETRY_LIMIT             ( 10 )

/**
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

int vCdfRegister(bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface )
{
    int status = EXIT_SUCCESS;
    int mqtt_connect_attempts;
    /* Handle of the MQTT connection used in this demo. */
    IotMqttConnection_t mqttConnection = IOT_MQTT_CONNECTION_INITIALIZER;

    configPRINTF( ( "JITR Creating MQTT Client...\r\n" ) );
    vTaskDelay( jitrONE_SECOND_DELAY_IN_TICKS );
    if( IotMqtt_Init() != IOT_MQTT_SUCCESS )
    {
        /* Failed to initialize MQTT library. */
        status = EXIT_FAILURE;
        configPRINTF( ( "IotMqtt_Init() not okay \r\n" ) );
        vTaskDelay( jitrONE_SECOND_DELAY_IN_TICKS );
    }
    else
    {
        configPRINTF( ( "IotMqtt_Init() okay \r\n" ) );
        vTaskDelay( jitrONE_SECOND_DELAY_IN_TICKS );
    }

    /* Code does not get to this point unless the network is up and running */
    xNetworkConnected = pdTRUE;

    for (mqtt_connect_attempts = 0; mqtt_connect_attempts < jitrCONN_RETRY_LIMIT;  )
    {
        if( xNetworkConnected == pdTRUE )
        {
            configPRINTF( ( "JITR Connecting to broker...\r\n" ) );

            /* Establish a new MQTT connection.
             *
             * Turn off last will and testament for JITR because policy does not 
             * include the the Publish rule required by LWT.
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
                IotMqtt_Disconnect( mqttConnection, false);
                break;
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

        vTaskDelay( 5 * jitrONE_SECOND_DELAY_IN_TICKS );
    }
    IotMqtt_Cleanup();
    return (status);
}

