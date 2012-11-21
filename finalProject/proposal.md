Onion Routing in Peerster
=========================

My final project is to implement onion routing. Messages are repeatedly
encrypted for each peerster in the path, and then sent to follow the path. Each
peerster removes a layer of encryption to discover routing instructions, and
sends the message to the next peersting in the path, until the message reaches
the destination. With the onion routing anonymous communication, intermediary 
peersters in the path shall not know the origin or any other peers except the 
next one, and the message also shall not be revealed to any intermediary node
in the path.

The implementation is based on the existing version of peerster, and will be
applied to P2P private messaging and file block sending.

Overview
-----------

To implement onion routing in peerster, I am going to follow these steps:

    1. Each peerster will generate a public key and private key when it is first
    created. 
    1. Each peerster can request and receive a public key from an another one.
    1. When a peerster wants to forward a new routing message, it will attach its
    own node ID and public keys to the message.
    1. When a peerster receives a routing message, it will record the origin
    node ID as well as the path and public keys.
    1. When a peerster wants to send a private message with onion routing, it
    first reads the path to the destination node, then encrypts the message with
    public keys layer by layer for each node along the path, and finally sends
    the message to the entry node.
    1. The intermediary node in the path receives the private message, and
    decrypts the message with its own private key. If it is the destination
    node, display the message. If it exceeds the hop limit, it would be
    disposed. Otherwise, it will be sends to the next node in the path.

