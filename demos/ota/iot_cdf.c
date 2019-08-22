/*
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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
 */

/**
 * @file iot_cdf.c
 * @brief Configuration Device Framework for JITR, Appliance Registration, and Certificate Rotation
 */

/* The config header is always included first. */
#include "iot_config.h"

/* Standard includes. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"
#include "platform/iot_threads.h"

/* MQTT include. */
#include "iot_mqtt.h"

/* IoT includes */
#include "aws_clientcredential_keys.h"
#include "aws_clientcredential.h"
#include "aws_iot_ota_agent.h"
#include "iot_cdf_agent.h"
#include "aws_dev_mode_key_provisioning.h"

static CDF_STATE _cdf_state;
static char _cdf_temp_certificate[ _CR_CERTIFICATE_SIZE ];
static char _cdf_certificate[ _CR_CERTIFICATE_SIZE ];
static char _cdf_private_key[ _CR_PRIVATE_KEY_SIZE ];
static char _cdf_csr[ _CR_CSR_SIZE ];
static char _cdf_new_certificate_id[_CERTIFICATE_ID_LENGTH];
static char _cdf_old_certificate_id[_CERTIFICATE_ID_LENGTH];


extern int vRunOTAUpdateDemo( cdf_Api_t *cdfApi,
                bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface );

extern int vCdfRegister(bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface );

extern int vCdfAppReg(bool awsIotMqttMode,
                const char * pIdentifier,
                void * pNetworkServerInfo,
                void * pNetworkCredentialInfo,
                const IotNetworkInterface_t * pNetworkInterface );

    /* Code does not get to this point unless the network is up and running */
extern BaseType_t xNetworkConnected;

extern void print_PEM(char * pemStr);

static uint8_t prvCDF_CustomerWriteState (CDF_STATE val)
{
    _cdf_state = val;
    return EXIT_SUCCESS;
}

static CDF_STATE prvCDF_CustomerReadState ()
{
    return _cdf_state;
}

static uint8_t _CustomerPutCert (char *cert_str, char *dst)
{
    int status = EXIT_FAILURE;
    char *cpTo, *cpFrom; 
    int foundSlash = false;

    if (strlen(cert_str) < _CR_CERTIFICATE_SIZE)
    {
        status = EXIT_SUCCESS;
        cpTo = dst;
        cpFrom = cert_str;
        while (*cpFrom)
        {
            if (foundSlash)
            {
                if  (*cpFrom == 'n')
                {
                    *cpTo++ = '\n' ;
                    cpFrom++;
                }
                else if (*cpFrom == 'r')
                {
                    *cpTo++ = '\r' ;
                    cpFrom++;
                }
                else 
                {
                    *cpTo++ = '\\' ;
                }
            }

            if (*cpFrom == '\\')
            {
                foundSlash = true;
                cpFrom++;
            }
            else
            {
                *cpTo++ = *cpFrom++;
                foundSlash = false;
            }
        }
        /* Null terminate */
        *cpTo = *cpFrom;
    }
    else
    {
        IotLogError( "prvCDF_CustomerPutDeviceCert: cert_str size = %d, _CR_CERTIFICATE_SIZE= %d.", strlen(cert_str), _CR_CERTIFICATE_SIZE); 
    }
    
    return status;
}

static uint8_t prvCDF_CustomerPutTempDeviceCert (char *cert_str)
{
    return(_CustomerPutCert(cert_str, &_cdf_temp_certificate[0]));
}
static uint8_t prvCDF_CustomerPutDeviceCert (char *cert_str)
{
    return(_CustomerPutCert(cert_str, &_cdf_certificate[0]));
}

static char * prvCDF_CustomerGetDeviceCert (void)
{
    return (&_cdf_certificate[0]);
}

static char * prvCDF_CustomerGetTempDeviceCert (void)
{
    return (&_cdf_temp_certificate[0]);
}

static uint8_t prvCDF_CustomerPutDevicePrivateKey (char *private_key_str)
{
    int status = EXIT_FAILURE;
    if (strlen(private_key_str) < _CR_PRIVATE_KEY_SIZE)
    {
        status = EXIT_SUCCESS;
        strcpy(&_cdf_private_key[0], private_key_str);
    }
    else
    {
        IotLogError( "prvCDF_CustomerPutDevicePrivateKey: bad private key size = %d, _CR_PRIVATE_KEY_SIZE = %d.", strlen(private_key_str), _CR_PRIVATE_KEY_SIZE );
    }
    return status;
}

static char * prvCDF_CustomerGetDevicePrivateKey ( void )
{
    return (&_cdf_private_key[0]);
}

static uint8_t prvCDF_CustomerPutCSR (char *csr_str)
{
    int status = EXIT_FAILURE;
    if (strlen(csr_str) < _CR_CSR_SIZE)
    {
        status = EXIT_SUCCESS;
        strcpy(&_cdf_csr[0], csr_str);
    }
    /*  IotLogError( "prvCDF_CustomerPutCSR : CSR size = %d, _CR_CSR_SIZE = %d.", strlen(csr_str), _CR_CSR_SIZE ); */
    return status;
}

static char * prvCDF_CustomerGetCSR ( void )
{
    char * ret_csr_str;
       
    IotLogInfo( "prvCDF_CustomerGetCSR: start");
    if (mbed_getCSR( &_cdf_csr[0], &_cdf_private_key[0], &_cdf_certificate[0] ))
    {
        ret_csr_str = &_cdf_csr[0];
#ifdef DEBUG_CSR_AND_CERT
        IotLogInfo("PRINT CSR"); 
        print_PEM(ret_csr_str);
#endif
    }
    else
    {
        ret_csr_str = NULL;
    }
    return (ret_csr_str);
}

static int prvCDF_CustomerRegisterDevice ( void )
{
    return EXIT_SUCCESS;
}

static int prvCDF_CustomerPutOldCertificateId(char *str){
    int status = EXIT_FAILURE;
    if(strlen(str) <= _CERTIFICATE_ID_LENGTH){
        strcpy(&_cdf_old_certificate_id, str);
        status = EXIT_SUCCESS;
    }

    return status;
}

static char * prvCDF_CustomerGetOldCertificateId(void){
    return (&_cdf_old_certificate_id[0]);
}

static int prvCDF_CustomerPutNewCertificateId(char *str){
    int status = EXIT_FAILURE;
    if(strlen(str) <= _CERTIFICATE_ID_LENGTH){
        strcpy(&_cdf_new_certificate_id, str);
        status = EXIT_SUCCESS;
    }

    return status;
}

static char * prvCDF_CustomerGetNewCertificateId(void){
    return (&_cdf_new_certificate_id[0]);
}

static void prvCdfInit( cdf_Api_t *cdfApi )
{
    ( * cdfApi->xWriteCDFStateNVM ) ( CDF_STATE_WAIT_FOR_CERT_ROTATE );
    /*
     * Customer needs to adjust these function calls. Particular
     * attention should be given to exposing the private key.
     * The CSR should be generted by calling GetCSR(), the PutCSR() shouldn't 
     * be used.
     */
    if (( * cdfApi->xPutDeviceCert ) ( keyCLIENT_CERTIFICATE_PEM ) == EXIT_SUCCESS)
    {
        if (( * cdfApi->xPutDevicePrivateKey ) ( keyCLIENT_PRIVATE_KEY_PEM ) == EXIT_SUCCESS)
        {
            /* Only ust the PutCSR if using a pre-generated CSR */
            if (( * cdfApi->xPutCSR ) ( keyCLIENT_CSR_PEM ) != EXIT_SUCCESS)
            {
                IotLogError("Did not write CSR");
            }
        }
        else
        {
            IotLogError("Did not write private key");
        }
    }
    else
    {
        IotLogError("Did not write cert");
    }
    
}

void _provisionCert( char *certStr, 
        cdf_Api_t * xpCdfApi, 
        IotNetworkCredentials_t * pNetworkCredentialInfo )
{
    ProvisioningParams_t xParams;
    IotNetworkCredentials_t * credentials; 

    IotLogInfo("_provisionCert");

    xParams.pucClientCertificate = ( uint8_t * ) certStr;
    xParams.ulClientCertificateLength = strlen( (char *) xParams.pucClientCertificate ) + 1;
    xParams.pucClientPrivateKey = ( uint8_t * ) (*xpCdfApi->xGetDevicePrivateKey)();
    xParams.ulClientPrivateKeyLength = strlen( (char *) xParams.pucClientPrivateKey ) + 1;

    vAlternateKeyProvisioning( &xParams );

    credentials = (IotNetworkCredentials_t *) pNetworkCredentialInfo; 
    credentials->pClientCert = (const char *) certStr;
    credentials->clientCertSize = strlen( credentials->pClientCert + 1);
}
        
        
/*-----------------------------------------------------------*/

/**
 * @brief The function that runs the CDF and OTA demo
 */
int vRunCDF_OTADemo ( bool awsIotMqttMode,
            const char * pIdentifier,
            void * pNetworkServerInfo,
            void * pNetworkCredentialInfo,
            const IotNetworkInterface_t * pNetworkInterface )
{
    int status;
#ifdef SAVE
    char *csr_str;
    int csr_str_len;
#endif

    /* All the data and Callback for the CDF Demo. */
    cdf_Api_t cdfApi = { \
        .xWriteCDFStateNVM          = prvCDF_CustomerWriteState, \
        .xReadCDFStateNVM           = prvCDF_CustomerReadState, \
        .xPutTempDeviceCert         = prvCDF_CustomerPutTempDeviceCert, \
        .xGetTempDeviceCert         = prvCDF_CustomerGetTempDeviceCert, \
        .xPutDeviceCert             = prvCDF_CustomerPutDeviceCert, \
        .xGetDeviceCert             = prvCDF_CustomerGetDeviceCert, \
        .xPutDevicePrivateKey       = prvCDF_CustomerPutDevicePrivateKey, \
        .xGetDevicePrivateKey       = prvCDF_CustomerGetDevicePrivateKey, \
        .xPutCSR                    = prvCDF_CustomerPutCSR, \
        .xGetCSR                    = prvCDF_CustomerGetCSR, \
        .xRegisterDevice            = prvCDF_CustomerRegisterDevice, \
        .xGetNewCertificateId       = prvCDF_CustomerGetNewCertificateId, \
        .xPutNewCertificateId       = prvCDF_CustomerPutNewCertificateId, \
        .xGetOldCertificateId       = prvCDF_CustomerGetOldCertificateId, \
        .xPutOldCertificateId       = prvCDF_CustomerPutOldCertificateId
    };

    IotLogInfo( "vRunCDF_OTADemo" );
    prvCdfInit( &cdfApi );

#ifdef SAVE
    IotLogInfo( "CDF State = %d", (*cdfApi.xReadCDFStateNVM)());
    IotLogInfo( "Device Cert = %100s", (*cdfApi.xGetDeviceCert)());
    IotLogInfo( "Private Key Cert = %100s", (*cdfApi.xGetDevicePrivateKey)());
    IotLogInfo( "CSR = %100s", (*cdfApi.xGetCSR)());
#endif 

    xNetworkConnected = pdTRUE;
    /* 
     * Ensure Appliance Registration and/or JITR is completed
     * per customer's unique requirements before devcie becomes 
     * operational.
     */
    status = EXIT_SUCCESS;
    while (status == EXIT_SUCCESS)
    {
        int state = (*cdfApi.xReadCDFStateNVM)();
        if ( (*cdfApi.xReadCDFStateNVM)() == CDF_STATE_JITR )        
        {
            IotLogInfo( "vRunCDF_OTADemo CDF_STATE_JITR ");
            IotClock_SleepMs( 1000 );
            _provisionCert((*cdfApi.xGetDeviceCert)(), &cdfApi, pNetworkCredentialInfo);
            status = vCdfRegister(awsIotMqttMode,
                    MAC_ADDR,
                    pNetworkServerInfo,
                    pNetworkCredentialInfo,
                    pNetworkInterface );

            if (status == EXIT_SUCCESS)
            {
                ( * cdfApi.xWriteCDFStateNVM ) ( CDF_STATE_APP_REG );
            }
            /* Product code should never leave this while loop, so always set status = EXIT_SUCCESS */
            /*  status = EXIT_SUCCESS; */
        }
        else if ( (*cdfApi.xReadCDFStateNVM)() == CDF_STATE_APP_REG )        
        {
            /* 
             * Device has been registered, now go register the appliance.
             *  
             * Note to connect with clientcredentialIOT_THING_NAME after this. JITR 
             * connects with MAC address.
             */
            IotLogInfo( "vRunCDF_OTADemo CDF_STATE_APP_REG ");
            IotClock_SleepMs( 1000 );
            _provisionCert((*cdfApi.xGetDeviceCert)(), &cdfApi, pNetworkCredentialInfo);
            status = vCdfAppReg(awsIotMqttMode,
                    clientcredentialIOT_THING_NAME,
                    pNetworkServerInfo,
                    pNetworkCredentialInfo,
                    pNetworkInterface );

            if (status == EXIT_SUCCESS)
            {
                ( * cdfApi.xWriteCDFStateNVM ) ( CDF_STATE_WAIT_FOR_CERT_ROTATE );
            }
            /* Product code should never leave this while loop, so always set status = EXIT_SUCCESS */
            /*  status = EXIT_SUCCESS; */
        }
        else if ( (*cdfApi.xReadCDFStateNVM)() == CDF_STATE_WAIT_FOR_CERT_ROTATE ) 
        {
            
            IotLogInfo( "vRunCDF_OTADemo CDF_STATE_WAIT_FOR_CERT_ROTATE ");
            IotClock_SleepMs( 1000 );
            _provisionCert((*cdfApi.xGetDeviceCert)(), &cdfApi, pNetworkCredentialInfo);
            status = vRunOTAUpdateDemo(&cdfApi,
                    awsIotMqttMode,
                    clientcredentialIOT_THING_NAME,
                    pNetworkServerInfo,
                    pNetworkCredentialInfo,
                    pNetworkInterface );

            if (status == EXIT_SUCCESS)
            {
                ( * cdfApi.xWriteCDFStateNVM ) ( CDF_STATE_ACK_CERT_ROTATE );
            }
#ifdef DEBUG_CSR_AND_CERT
            IotLogInfo("CDF_STATE_WAIT_FOR_CERT_ROTATE TEMP CERT"); 
            print_PEM((*cdfApi.xGetTempDeviceCert)());
            IotLogInfo("CDF_STATE_WAIT_FOR_CERT_ROTATE DEVICE CERT"); 
            print_PEM((*cdfApi.xGetDeviceCert)());
#endif
            /* Product code should never leave this while loop, so always set status = EXIT_SUCCESS */
            status = EXIT_SUCCESS;
        }
        else if ( ((*cdfApi.xReadCDFStateNVM)() == CDF_STATE_ACK_CERT_ROTATE ) )
        {
            IotLogInfo( "vRunCDF_OTADemo CDF_STATE_ACK_CERT_ROTATE ");
            IotClock_SleepMs( 1000 );
//            _provisionCert((*cdfApi.xGetDeviceCert)(), &cdfApi, pNetworkCredentialInfo);
            status = vRunOTAUpdateDemo(&cdfApi,
                    awsIotMqttMode,
                    clientcredentialIOT_THING_NAME,
                    pNetworkServerInfo,
                    pNetworkCredentialInfo,
                    pNetworkInterface );

            if (status == EXIT_SUCCESS)
            {
                /* You have successfully receieved the new certificate, write to the new state */
                ( * cdfApi.xWriteCDFStateNVM) ( CDF_STATE_DEACTIVATE_CERT );
            }
#ifdef DEBUG_CSR_AND_CERT
            IotLogInfo("CDF_STATE_ACK_CERT_ROTATE TEMP CERT"); 
            print_PEM((*cdfApi.xGetTempDeviceCert)());
            IotLogInfo("CDF_STATE_ACK_CERT_ROTATE DEVICE CERT"); 
            print_PEM((*cdfApi.xGetDeviceCert)());
#endif
            status = EXIT_SUCCESS;
        }
        else if ( ((*cdfApi.xReadCDFStateNVM)() == CDF_STATE_DEACTIVATE_CERT ) )
        {
            IotLogInfo( "vRunCDF_OTADemo CDF_STATE_ACK_CERT_ROTATE ");
            IotClock_SleepMs( 1000 );
            _provisionCert((*cdfApi.xGetTempDeviceCert)(), &cdfApi, pNetworkCredentialInfo);
            status = vRunOTAUpdateDemo(&cdfApi,
                    awsIotMqttMode,
                    clientcredentialIOT_THING_NAME,
                    pNetworkServerInfo,
                    pNetworkCredentialInfo,
                    pNetworkInterface );

            if (status == EXIT_SUCCESS)
            {
                /* Successful cert rotation, verify the old cert no longer works */
                ( * cdfApi.xPutDeviceCert) ( ( * cdfApi.xGetTempDeviceCert)() );
                ( * cdfApi.xWriteCDFStateNVM ) ( CDF_STATE_FINISHED );
            }
            status = EXIT_SUCCESS;
#ifdef DEBUG_CSR_AND_CERT
            IotLogInfo("CDF_STATE_ACK_CERT_ROTATE TEMP CERT"); 
            print_PEM((*cdfApi.xGetTempDeviceCert)());
            IotLogInfo("CDF_STATE_ACK_CERT_ROTATE DEVICE CERT"); 
            print_PEM((*cdfApi.xGetDeviceCert)());
#endif
        }
        else if ( ((*cdfApi.xReadCDFStateNVM)() == CDF_STATE_FINISHED ) ){
            #define keyCLIENT_CERTIFICATE_PEM  *cdfApi.xGetDeviceCert();
            break;
        }
    }
    return(status);
}
