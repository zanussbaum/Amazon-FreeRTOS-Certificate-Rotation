import json
import boto3
import logging
from botocore.config import Config

logger = logging.getLogger()
logger.setLevel(logging.INFO)


def create_and_attach(event, context):
    """
    This function creates and attaches a new certificate created
    from a CSR to existing policies and things

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
        old_certificate_id = event['principal']
        logger.info(("the expired certificate id is %s" % old_certificate_id))

        iot_client = boto3.client('iot', config=config)

        certificate_response = create_certificate(iot_client, event)

        new_certificate_arn = certificate_response['certificateArn']
        new_certificate_id = certificate_response['certificateId']
        new_certificate_pem = certificate_response['certificatePem']
        logger.info(("created certificate with id %s" % new_certificate_id))

        old_certificate_arn = iot_client.describe_certificate(
            certificateId=old_certificate_id
        )['certificateDescription']['certificateArn']

        policies_response = list_policies(iot_client, old_certificate_arn)

        logger.info('got attached policies')

        thing_response = list_things(iot_client, old_certificate_arn)

        logger.info('got attached things')

        for full_page_policy in policies_response:
            for policy in full_page_policy:
                logger.info(("attaching certificate %s to policy %s" % (new_certificate_arn, policy['policyName'])))

                iot_client.attach_policy(
                    policyName=policy['policyName'],
                    target=new_certificate_arn
                )

        for full_page_thing in thing_response:
            for thing in full_page_thing:
                logger.info(("attaching certificate %s to thing %s" % (new_certificate_arn, thing)))
                iot_client.attach_thing_principal(
                    thingName=thing,
                    principal=new_certificate_arn
                )

        message = {'newCertificateArn': new_certificate_arn, 'newCertificateId': new_certificate_id,
                   'newCertificatePem': new_certificate_pem,
                   'oldCertificateId': old_certificate_id,
                   }

        logger.info("new certificate information: ARN:{}, ID:{}, and PEM:{}".format(
            message['newCertificateArn'], message['newCertificateId'], message['newCertificatePem']))

        publish_response(event['clientId'], message, config)
        return
    except KeyError as e:
        logger.error("raised error {}. Make sure to supply the {} in the payload.".format(e, e))
        message = {"error": "user failed to supply the {}".format(e)}
        publish_response(event['clientId'], message, config)
        return
    except Exception as e:
        logger.error(("raised error {}. Failed to create and attach the new certificate".format(e)))
        message = {"error": "{}".format(e)}
        publish_response(event['clientId'], message, config)
        return


def create_certificate(iot_client, event):
    """
    Creates a certificate from a CSR
    :param iot_client: the boto client to connect and execute
    :param event: the dictionary sent to Lambda by the Rules
    Engine
    :return: certificate response (dictionary)
    """

    csr_string = event['response']['csr']
    response = iot_client.create_certificate_from_csr(
        certificateSigningRequest=csr_string,
        setAsActive=False
    )
    logger.info('created certificate')

    return response


def list_things(iot_client, old_certificate_arn):
    """
    Creates a list of all things attached to the old certificate
    :param iot_client: the boto client to connect and execute
    IoT APIs
    :param old_certificate_arn: the old certificate arn
    :return: list of things
    """
    things_list = []
    response = iot_client.list_principal_things(
        principal=old_certificate_arn
    )
    things_list.append(response['things'])

    while 'nextToken' in response:
        response = iot_client.list_principal_things(
            nextToken=response['nextToken'],
            principal=old_certificate_arn
        )
        things_list.append(response['things'])

    return things_list


def list_policies(iot_client, old_certificate_arn):
    """
    Creates a list of all the policies attached to the old certificate
    :param iot_client: the boto client to connect and execute
    IoT APIs
    :param old_certificate_arn: the old certificate arn
    :return: list of policies
    """
    policy_list = []
    response = iot_client.list_attached_policies(
        target=old_certificate_arn
    )
    policy_list.append(response['policies'])

    while 'nextMarker' in response:
        response = iot_client.list_attached_policies(
            marker=response['nextMarker'],
            target=old_certificate_arn
        )
        policy_list.append(response['policies'])

    return policy_list


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
        logger.error(("raised error {}. Failed to publish message".format(e)))

    return
