#!/usr/bin/env python
"""
Mock HTTP Registration Server for testing res_rpt_http_registrations

This server simulates the registration endpoint that accepts node
registration requests and returns appropriate responses for testing.
"""

import json
import sys
from twisted.web import server, resource
from twisted.internet import reactor, ssl
from OpenSSL import SSL

class RegistrationHandler(resource.Resource):
    """
    Handle HTTP POST requests for node registration.
    Simulates the actual registration server behavior.
    """
    isLeaf = True

    def __init__(self):
        resource.Resource.__init__(self)
        self.registrations = {}
        self.registration_count = 0

    def render_POST(self, request):
        """
        Handle POST request for registration.

        Expected request format:
        {
            "port": 4569,
            "data": {
                "nodes": {
                    "node_number": {
                        "node": "node_number",
                        "passwd": "password",
                        "remote": 0
                    }
                }
            }
        }

        Response format:
        {
            "ipaddr": "client_ip",
            "port": 4569,
            "refresh": 60,
            "data": "successfully registered"
        }
        """
        # Set response content type
        request.setHeader('Content-Type', 'application/json')

        try:
            # Parse request body
            content = request.content.read()
            data = json.loads(content)

            # Extract client information
            client_ip = request.getClientAddress().host
            client_port = data.get('port', 4569)

            # Extract node registration data
            node_data = data.get('data', {}).get('nodes', {})

            # Store registration
            for node_id, node_info in node_data.items():
                self.registrations[node_id] = {
                    'username': node_info.get('node', ''),
                    'password': node_info.get('passwd', ''),
                    'ip': client_ip,
                    'port': client_port,
                    'remote': node_info.get('remote', 0)
                }
                self.registration_count += 1

                print(f"Registration received: node={node_id}, "
                      f"username={node_info.get('node', '')}, "
                      f"ip={client_ip}:{client_port}")

            # Build success response
            response = {
                'ipaddr': client_ip,
                'port': client_port,
                'refresh': 60,
                'data': 'successfully registered'
            }

            return json.dumps(response).encode('utf-8')

        except json.JSONDecodeError as e:
            print(f"JSON decode error: {e}")
            request.setResponseCode(400)
            return json.dumps({'error': 'Invalid JSON'}).encode('utf-8')
        except Exception as e:
            print(f"Error processing registration: {e}")
            request.setResponseCode(500)
            return json.dumps({'error': 'Internal server error'}).encode('utf-8')

    def render_GET(self, request):
        """
        Handle GET requests - return current registration status.
        Useful for debugging.
        """
        request.setHeader('Content-Type', 'application/json')
        return json.dumps({
            'registrations': self.registrations,
            'total_count': self.registration_count
        }).encode('utf-8')


class FailureHandler(resource.Resource):
    """
    Handler that simulates various failure scenarios.
    Used for negative testing.
    """
    isLeaf = True

    def __init__(self, status_code=500, message="Internal Server Error"):
        resource.Resource.__init__(self)
        self.status_code = status_code
        self.message = message

    def render_POST(self, request):
        request.setResponseCode(self.status_code)
        request.setHeader('Content-Type', 'application/json')
        return json.dumps({'error': self.message}).encode('utf-8')


def create_ssl_context():
    """
    Create a simple SSL context for HTTPS support.
    Uses self-signed certificate for testing.
    """
    # Create a self-signed certificate context
    context_factory = ssl.DefaultOpenSSLContextFactory(
        '/tmp/server.key',  # Private key
        '/tmp/server.crt',  # Certificate
    )
    return context_factory


def generate_self_signed_cert():
    """
    Generate a self-signed certificate for testing.
    """
    from OpenSSL import crypto
    import os

    # Generate key
    key = crypto.PKey()
    key.generate_key(crypto.TYPE_RSA, 2048)

    # Generate certificate
    cert = crypto.X509()
    cert.get_subject().C = "US"
    cert.get_subject().ST = "Test"
    cert.get_subject().L = "Test"
    cert.get_subject().O = "Test"
    cert.get_subject().OU = "Test"
    cert.get_subject().CN = "localhost"
    cert.set_serial_number(1000)
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(10*365*24*60*60)  # Valid for 10 years
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(key)
    cert.sign(key, 'sha256')

    # Write key and certificate
    with open('/tmp/server.key', 'wb') as f:
        f.write(crypto.dump_privatekey(crypto.FILETYPE_PEM, key))

    with open('/tmp/server.crt', 'wb') as f:
        f.write(crypto.dump_certificate(crypto.FILETYPE_PEM, cert))

    print("Generated self-signed certificate for testing")


def main():
    """
    Start the mock registration server.
    """
    port = 8443  # Default HTTPS port for testing

    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            print(f"Invalid port number: {sys.argv[1]}")
            sys.exit(1)

    # Generate self-signed certificate if needed
    import os
    if not os.path.exists('/tmp/server.key') or not os.path.exists('/tmp/server.crt'):
        try:
            generate_self_signed_cert()
        except Exception as e:
            print(f"Warning: Could not generate SSL certificate: {e}")
            print("Running in HTTP mode instead")

    # Create resource tree
    root = resource.Resource()
    root.putChild(b'', RegistrationHandler())
    root.putChild(b'fail', FailureHandler(status_code=500))
    root.putChild(b'unauthorized', FailureHandler(status_code=401, message="Unauthorized"))
    root.putChild(b'notfound', FailureHandler(status_code=404, message="Not Found"))

    site = server.Site(root)

    # Try HTTPS first, fall back to HTTP if SSL unavailable
    try:
        if os.path.exists('/tmp/server.key') and os.path.exists('/tmp/server.crt'):
            context_factory = create_ssl_context()
            reactor.listenSSL(port, site, context_factory)
            print(f"Mock HTTPS registration server listening on port {port}")
        else:
            raise Exception("SSL files not found")
    except Exception as e:
        print(f"SSL not available ({e}), using HTTP instead")
        reactor.listenTCP(port, site)
        print(f"Mock HTTP registration server listening on port {port}")

    print("Press Ctrl+C to stop")
    reactor.run()


if __name__ == '__main__':
    main()