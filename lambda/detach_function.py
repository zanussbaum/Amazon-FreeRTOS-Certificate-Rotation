from botocore.config import Config
import json
import boto3
import logging
import re

logger = logging.getLogger()
logger.setLevel(logging.INFO)


def deactivate_certificate(event, context):
    """
    This function sets the old certificate as inactive

    :param event: A dictionary sent to the Lambda function
    from the rules engine, taken from the message published by the client
    i.e. {'principal': 'oldCertificateId', 'response': {csr': 'CSR sent'},
    'clientId': 'client'}
    :param context: An object passed by Lambda that provides information
    about the invocation, function, and execution environment.

    :return: None
    """
    config = Config(
        retries={
            'max_attempts': 4
        }
    )
    try:
        client_id = event['clientId']

        old_certificate_id = event['response']['oldCertificateId']
        is_valid_certificate_id = re.fullmatch('(0x)?[a-fA-F0-9]+', old_certificate_id)

        if is_valid_certificate_id is None or len(old_certificate_id) != 64:
            message = 'you have passed an invalid old certificate id'
            logger.error(message)
            publish_response(event['clientId'], message, config)
            return

        logger.info(("the old certificate id is %s" % old_certificate_id))

        new_certificate = event['principal']

        if new_certificate == old_certificate_id:
            message = {"error": "You must connect with your new certificate to deactivate",
                       "clientId": event['clientId']}
            logger.error('You must connect with your new certificate to deactivate')
            publish_response(event['clientId'], message, config)
            return

        iot_client = boto3.client('iot', config=config)

        iot_client.update_certificate(
            certificateId=old_certificate_id,
            newStatus='INACTIVE'
        )

        result = old_certificate_id + " was successfully set as inactive"
        message = {"result": result}
        logger.info(result)
        publish_response(client_id, message, config)
        return

    except KeyError as e:
        logger.error("raised error {}. Make sure to supply the {} in the payload.".format(e, e))
        message = {"error": "user failed to supply the {}".format(e)}
        publish_response(client_id, message, config)
        return
    except Exception as e:
        result_message = 'you must manually deactivate the certificate with id ' + old_certificate_id
        error_message = {"error": "{}".format(e),
                         'result': result_message}
        logger.error('raised error {}. Failed to deactivate certificate {}'.format(e, old_certificate_id))
        publish_response(client_id, error_message, config)
        return


def publish_response(client_id, message, config):
    """
    Publishes a response to the client

    :param client_id: string to publish to correct topic
    :param message: dictionary with response to send back
    :param config: dictionary of retry setting
    :return: None
    """
    publish_client = boto3.client('iot-data', config=config)

    topic = 'certificate/rotation/result/' + client_id
    logger.info("publishing to topic %s" % topic)
    try:
        publish_client.publish(
            topic=topic,
            qos=1,
            payload=json.dumps(message)
        )
    except Exception as e:
        logger.error('raised error {}. Failed to publish message'.format(e))

    return
