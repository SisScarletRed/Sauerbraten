// client.cpp, mostly network related client game code

#include "engine.h"

ENetHost *clienthost = NULL;
ENetPeer *curpeer = NULL, *connpeer = NULL;
int connmillis = 0, connattempts = 0, discmillis = 0;

int getpacketloss()
{
    return curpeer ? curpeer->packetsLost : 0;
}
ICOMMAND(getpacketslost, "", (), intret(getpacketloss()));

bool multiplayer(bool msg)
{
    bool val = curpeer || hasnonlocalclients();
    if(val && msg) conoutf(CON_ERROR, "operation not available in multiplayer");
    return val;
}
ICOMMAND(multiplayer, "", (), intret(multiplayer(false) ? 1 : 0);)

void setrate(int rate);
QVARF(rate, "set the enet_host_bandwith_limit-rate", 0, 0, 1024, setrate(rate));

void throttle();
VARFP(disableenetlimits, 0, 0, 1, throttle(); setrate(rate););

void setrate(int rate)
{
    if(!curpeer) return;
    if(disableenetlimits)
        enet_host_bandwidth_limit(clienthost, 0, 0);
    else
        enet_host_bandwidth_limit(clienthost, rate*1024, rate*1024);
}

VARF(throttle_interval, 0, 5, 30, throttle());
VARF(throttle_accel,    0, 2, 32, throttle());
VARF(throttle_decel,    0, 2, 32, throttle());

void throttle()
{
    if(!curpeer) return;
    ASSERT(ENET_PEER_PACKET_THROTTLE_SCALE==32);
    if(disableenetlimits)
        enet_peer_throttle_configure(curpeer, 1000, 32, 0);
    else
        enet_peer_throttle_configure(curpeer, throttle_interval*1000, throttle_accel, throttle_decel);
}

bool isconnected(bool attempt, bool local)
{
    return curpeer || (attempt && connpeer) || (local && haslocalclients());
}

QICOMMAND(isconnected, "returns if you are connected", "(attempt),(local)", "bb", (int *attempt, int *local), intret(isconnected(*attempt > 0, *local != 0) ? 1 : 0));

const ENetAddress *connectedpeer()
{
    return curpeer ? &curpeer->address : NULL;
}

QICOMMAND(connectedip, "returns the IP of the connected server", "", "", (),
{
    const ENetAddress *address = connectedpeer();
    string hostname;
    result(address && enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0 ? hostname : "");
});

QICOMMAND(connectedport, "returns the port of the connected server", "", "", (),
{
    const ENetAddress *address = connectedpeer();
    intret(address ? address->port : -1);
});

void abortconnect()
{
    if(!connpeer) return;
    game::connectfail();
    if(connpeer->state!=ENET_PEER_STATE_DISCONNECTED) enet_peer_reset(connpeer);
    connpeer = NULL;
    if(curpeer) return;
    enet_host_destroy(clienthost);
    clienthost = NULL;
}

QSVARP(connectname, "the name of the server you're connecting to", "");
QVARP(connectport, "the port of the server you're connecting to", 0, 0, 0xFFFF);

void connectserv(const char *servername, int serverport, const char *serverpassword)
{
    if(connpeer)
    {
        conoutf("aborting connection attempt");
        abortconnect();
    }

    if(serverport <= 0) serverport = server::serverport();

    ENetAddress address;
    address.port = serverport;

    if(servername)
    {
        if(strcmp(servername, connectname)) setsvar("connectname", servername);
        if(serverport != connectport) setvar("connectport", serverport);
        addserver(servername, serverport, serverpassword && serverpassword[0] ? serverpassword : NULL);
        conoutf("attempting to connect to %s:%d", servername, serverport);
        if(!resolverwait(servername, &address))
        {
            conoutf("\f3could not resolve server %s", servername);
            return;
        }
    }
    else
    {
        setsvar("connectname", "");
        setvar("connectport", 0);
        conoutf("attempting to connect over LAN");
        address.host = ENET_HOST_BROADCAST;
    }

    if(!clienthost)
    {
        clienthost = enet_host_create(NULL, 2, server::numchannels(), rate*1024, rate*1024);
        if(!clienthost)
        {
            conoutf("\f3could not connect to server");
            return;
        }
        clienthost->duplicatePeers = 0;
    }

    connpeer = enet_host_connect(clienthost, &address, server::numchannels(), 0);
    enet_host_flush(clienthost);
    connmillis = totalmillis;
    connattempts = 0;

    game::connectattempt(servername ? servername : "", serverpassword ? serverpassword : "", address);
}

void reconnect(const char *serverpassword)
{
    if(!connectname[0] || connectport <= 0)
    {
        conoutf(CON_ERROR, "no previous connection");
        return;
    }

    connectserv(connectname, connectport, serverpassword);
}

void disconnect(bool async, bool cleanup)
{
    if(curpeer)
    {
        if(!discmillis)
        {
            enet_peer_disconnect(curpeer, DISC_NONE);
            enet_host_flush(clienthost);
            discmillis = totalmillis;
        }
        if(curpeer->state!=ENET_PEER_STATE_DISCONNECTED)
        {
            if(async) return;
            enet_peer_reset(curpeer);
        }
        curpeer = NULL;
        discmillis = 0;
        conoutf("disconnected");
        game::gamedisconnect(cleanup);
        mainmenu = 1;
    }
    if(!connpeer && clienthost)
    {
        enet_host_destroy(clienthost);
        clienthost = NULL;
    }
}

QVARP(autosaydisconnect, "automatically say *autosaydisconnectmsg* on disconnect", 0, 0, 1);
QSVARP(autosaydisconnectmsg, "say this string on disconnect (if *autosaydisconnect is enabled)", "bye");

void trydisconnect(bool local)
{
    if(connpeer)
    {
        conoutf("aborting connection attempt");
        abortconnect();
    }
    else if(curpeer)
    {
        conoutf("attempting to disconnect...");
        if(autosaydisconnect)
            game::toserver(autosaydisconnectmsg);
        disconnect(!discmillis);
    }
    else if(local && haslocalclients()) localdisconnect();
    else conoutf("not connected");
}

QICOMMAND(connect, "connect to the specified server", "name,port,(password)", "sis", (char *name, int *port, char *pw), connectserv(name, *port, pw));
QICOMMAND(lanconnect, "connect to a LAN-server", "port,(password)", "is", (int *port, char *pw), connectserv(NULL, *port, pw));
QCOMMAND(reconnect, "reconnect to the last server", "(password)", "s");
QICOMMAND(disconnect, "disconnect from your server", "local", "i", (int *local), trydisconnect(*local != 0));
QICOMMAND(localconnect, "connect to a local server", "", "", (), { if(!isconnected()) localconnect(); });
QICOMMAND(localdisconnect, "disconnect from a local server", "", "", (), { if(haslocalclients()) localdisconnect(); });
ICOMMAND(islocal, "", (), intret(haslocalclients() ? 1 : 0));

void sendclientpacket(ENetPacket *packet, int chan)
{
    ASSERT(packet->dataLength)
    if(curpeer) enet_peer_send(curpeer, chan, packet);
    else localclienttoserver(chan, packet);
}

void flushclient()
{
    if(clienthost) enet_host_flush(clienthost);
}

void neterr(const char *s, bool disc)
{
    conoutf(CON_ERROR, "\f3illegal network message (%s)", s);
    if(disc) disconnect();
}

void localservertoclient(int chan, ENetPacket *packet)   // processes any updates from the server
{
    packetbuf p(packet);
    game::parsepacketclient(chan, p);
}

void clientkeepalive() { if(clienthost) enet_host_service(clienthost, NULL, 0); }

QVARP(connectattempts, "maximum number of attempts to connect to a server", 1, 3, 10);

void gets2c()           // get updates from the server
{
    ENetEvent event;
    if(!clienthost) return;
    if(connpeer && totalmillis/3000 > connmillis/3000)
    {
        conoutf("attempting to connect...");
        connmillis = totalmillis;
        ++connattempts;
        if(connattempts > connectattempts)
        {
            conoutf("\f3could not connect to server");
            abortconnect();
            return;
        }
    }
    while(clienthost && enet_host_service(clienthost, &event, 0)>0)
    switch(event.type)
    {
        case ENET_EVENT_TYPE_CONNECT:
            disconnect(false, false);
            localdisconnect(false);
            curpeer = connpeer;
            connpeer = NULL;
            conoutf("connected to server");
            throttle();
            if(rate) setrate(rate);
            game::gameconnect(true);
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            if(discmillis) conoutf("attempting to disconnect...");
            else localservertoclient(event.channelID, event.packet);
            enet_packet_destroy(event.packet);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            if(event.data>=DISC_NUM) event.data = DISC_NONE;
            if(event.peer==connpeer)
            {
                conoutf("\f3could not connect to server");
                abortconnect();
            }
            else
            {
                if(!discmillis || event.data)
                {
                    const char *msg = disconnectreason(event.data);
                    if(msg) conoutf("\f3server network error, disconnecting (%s) ...", msg);
                    else conoutf("\f3server network error, disconnecting...");
                }
                disconnect();
            }
            return;

        default:
            break;
    }
}

