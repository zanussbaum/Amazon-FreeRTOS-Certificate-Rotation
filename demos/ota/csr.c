/*
 * Amazon FreeRTOS V1.4.7
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
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

#include "iot_config.h"
#include "string.h"

/* Set up logging for this demo. */
#include "iot_demo_logging.h"

/* FreeRTOS includes. */

#include "FreeRTOS.h"
#include "task.h"

/* MQTT include. */
#include "iot_mqtt.h"

/* Platform layer includes. */
#include "platform/iot_clock.h"

/* Demo includes */
#include "aws_clientcredential_keys.h"
#include "aws_clientcredential.h"
#include "aws_iot_ota_agent.h"
#include "iot_cdf_agent.h"


#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/x509_csr.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/pk.h"

#define PRINT_DELAY_SMALL 5
#define PRINT_DELAY_BIG 5
#define PRINT_DELAY_BIGGEST 1000

#ifndef SAVE
void my_print1(char * str, int i)
{
    IotLogInfo( "%s %d", str, i);
    vTaskDelay( PRINT_DELAY_SMALL );
}

void my_print_2(char * str, char * str1)
{
    IotLogInfo( "%s %s", str, str1);
    vTaskDelay( PRINT_DELAY_SMALL );
}

void my_print(char * str)
{
    IotLogInfo( "%s", str);
    vTaskDelay( PRINT_DELAY_BIG );
}
#endif

void print_PEM(char * pemStr)
{
    int pemLen, i;
    char buffer[100];
    int j;
    int foundNewLine;
    int foundSlash;
    int ascii_new_lines = false;
    int ascii_CR = false;
    char * cpTo;
    char * cpFrom;

    IotLogInfo(" ---- start PEM: len = %d ----- ", strlen(pemStr));
    pemLen = strlen(pemStr);

    for (i = 0; i < pemLen; )
    {
        j = 0;
        foundNewLine = false;
        foundSlash = false;
        cpTo = &buffer[0];
        cpFrom = (pemStr + i);
        while (j < 100 && !foundNewLine)
        {
            *cpTo = *cpFrom;
            if (foundSlash && *cpTo == 'n')
            {
                foundNewLine = true;
            }
            else 
            {
                foundSlash = false;
            }
            if (*cpTo == '\\')
            {
                foundSlash = true;
            }
            if (*cpTo == '\n')
            {
                foundNewLine = true;
                ascii_new_lines = true;
            }
            if (*cpTo == '\r')
            {
                ascii_CR = true;
            }
            j++;
            cpTo++;
            cpFrom++;
        }
        buffer[j] = '\0'; 
        IotLogInfo("%-100s\r\n", buffer);
        i += j;
    }
    IotLogInfo(" ---- end PEM: -----\n");
    IotLogInfo(" ---- found ascii new lines %d -----\n", ascii_new_lines);
    IotLogInfo(" ---- found ascii CR %d -----\n", ascii_CR);
    IotClock_SleepMs( 500 );
}

extern int convert_pem_to_der( const unsigned char * pucInput,
                        size_t xLen,
                        unsigned char * pucOutput,
                        size_t * pxOlen );

char private_key_der_str[ _CR_PRIVATE_KEY_SIZE ];
/**
 * @brief Use Mbedtls library to generate a CSR.
 */
int mbed_getCSR( char *csr_str, char *private_key_pem_str, char *certificate_str )
{
    mbedtls_pk_context key;
    mbedtls_x509write_csr req;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    int xReturn = true;
    int ret;
    const char *pers = "csr example";
    int private_key_pem_str_len;
    size_t private_key_der_str_len = _CR_PRIVATE_KEY_SIZE;
 
    IotLogInfo("mbed_getCSR: Initialization"); 

    /* 
     * WARNING: set both entropy and key to all zeros.
i    *
     * Note that other _init() funcs clear the memory inside the function,
     * but mbedtls_pk_init() and mbedtls_entropy_init() don't clear all 
     * the memory. A critical thing to set to zero is a semaphore member
     * of the struct, otherwise a low level routine thinks a semaphore 
     * has already been initialized, which causes the CSR gen to fail.
     */
    memset( &entropy, 0, sizeof( mbedtls_entropy_context ) );
    memset( &key, 0, sizeof( mbedtls_pk_context ) );

    mbedtls_pk_init( &key );
    mbedtls_x509write_csr_init( &req );
    mbedtls_ctr_drbg_init( &ctr_drbg );
    mbedtls_entropy_init( &entropy );

    mbedtls_x509write_csr_set_md_alg( &req, MBEDTLS_MD_SHA256 );
    mbedtls_x509write_csr_set_key_usage( &req, MBEDTLS_X509_KU_DIGITAL_SIGNATURE );
    mbedtls_x509write_csr_set_ns_cert_type( &req, MBEDTLS_X509_NS_CERT_TYPE_SSL_CLIENT );

    /*
     * Seed the PRNG
     */
    private_key_pem_str_len = strlen(private_key_pem_str);
    if( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
                               (const unsigned char *) pers,
                               strlen( pers ) ) ) != 0 )
    {
        IotLogError( "mbed_getCSR: Failed mbedtls_ctr_drbg_seed() returned %d.", ret );
        xReturn = false;
    }

    if (xReturn)
    {
        /*
         * Check the subject name for validity. Subject string cannot have spaces in the wrong places.
         * e.g. "C = CH, O = Brev Demo, CN = www.brevDemo.org" is an invalid subject.
         */
        if( ( ret = mbedtls_x509write_csr_set_subject_name( &req, "C=CH,O=Brev Demo,CN=www.brevDemo.org" ) ) != 0 )
        {
            IotLogError( "mbed_getCSR: Failed mbedtls_x509write_csr_set_subject_name() returned %d.", ret );
            xReturn = false;
        }
    }

    if (xReturn)
    {
        /*
         * Convert the PEM to DER format. This requires format passed into the function is PEM.
         */
        if ( (ret = convert_pem_to_der( 
                    (const unsigned char * ) private_key_pem_str,
                    private_key_pem_str_len,
                    (unsigned char *) private_key_der_str,
                    &private_key_der_str_len ) )  != 0 )
        {
            IotLogError( "mbed_getCSR: Failed convert_pem_to_der() returned %d.", ret );
            xReturn = false;
        }
    }

    if (xReturn)
    {
        /*
         * Load the private key
         */
        ret = mbedtls_pk_parse_key( &key, (const unsigned char *) private_key_der_str, private_key_der_str_len, NULL, 0);

        if( ret != 0 )
        {
            IotLogError( "mbed_getCSR: Failed mbedtls_pk_parse_key returned %d.", ret );
            xReturn = false;
        }
    }

    mbedtls_x509write_csr_set_key( &req, &key );

    if (xReturn)
    {
        /*
         * Write the request
         */
        memset( csr_str, 0, _CR_CSR_SIZE );

        if( ( ret = mbedtls_x509write_csr_pem( &req, (unsigned char *) csr_str, _CR_CSR_SIZE, mbedtls_ctr_drbg_random, &ctr_drbg ) ) < 0 )
        {
            IotLogError( "mbed_getCSR: Failed mbedtls_x509write_csr_pem returned %d.", ret );
            xReturn = false;
        }
    }

    mbedtls_x509write_csr_free( &req );
    mbedtls_pk_free( &key );
    mbedtls_ctr_drbg_free( &ctr_drbg );
    mbedtls_entropy_free( &entropy );

    return xReturn;
}

