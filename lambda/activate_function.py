import boto3
import logging
import json
from botocore.config import Config

logger = logging.getLogger()
logger.setLevel(logging.INFO)


def activate(event, context):
    """
    This function activates the newly created certificate.
    This is to prevent from a customer requesting new certificates and
    creating many new active certificates, if the customer were to never
    receive the message back from the Lambda

    :param event: A dictionary sent to the Lambda function
    from the rules engine, taken from the message published by the client
    i.e. {'principal': 'oldCertificateId', 'response': {publicKey': 'publicKey sent'},
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
        iot_client = boto3.client('iot', config=config)
        iot_client.update_certificate(
            certificateId=event['response']['newCertificateId'],
            newStatus='ACTIVE'
        )
        message = 'certificate {} was set as active'.format(event['response']['newCertificateId'])
        logger.info(message)
        publish_response(event['clientId'], message, config)
    except KeyError as e:
        logger.error("raised error {}. Make sure to supply the {} in the payload.".format(e, e))
        message = {"error": "user failed to supply the new certificate Id"}
        publish_response(event['clientId'], message, config)
        return
    except Exception as e:
        logger.error('raised error {}. Failed to activate certificate'.format(e))
        message = {"error": "{}".format(e)}
        publish_response(event['clientId'], message, config)
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
            payload=json.dumps(message, default=lambda x: x.__dict__)
        )
    except Exception as e:
        error_message = 'raised error {}. Failed to publish message'.format(e)
        logger.error(error_message)

    return
