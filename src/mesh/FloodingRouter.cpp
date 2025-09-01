#include "FloodingRouter.h"

#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/portnums.pb.h" //meshtastic_PortNum_TELEMETRY_APP
FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;

        /* If the original transmitter is doing retransmissions (hopStart equals hopLimit) for a reliable transmission, e.g., when
        the ACK got lost, we will handle the packet again to make sure it gets an implicit ACK. */
        bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
        if (isRepeated) {
            LOG_DEBUG("Repeated reliable tx");
            // Check if it's still in the Tx queue, if not, we have to relay it again
            if (!findInTxQueue(p->from, p->id))
                perhapsRebroadcast(p);
        } else {
            perhapsCancelDupe(p);
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
}

void FloodingRouter::perhapsCancelDupe(const meshtastic_MeshPacket *p)
{
    if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
        config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE &&
        p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA) {
        // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
        // But only LoRa packets should be able to trigger this.
        if (Router::cancelSending(p->from, p->id))
            txRelayCanceled++;
    }
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
        iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
    }
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

bool FloodingRouter::shouldDropRecentPacket(NodeNum from, TrackedPacketType type)
{
    uint32_t currentTime = millis();
    uint32_t timeout = (type == TrackedPacketType::ENCRYPTED) ? ENCRYPTED_TIMEOUT_MS : PACKET_TIMEOUT_MS;
    auto& nodes = trackedNodes[static_cast<int>(type)];

    // Check if node exists in our tracking array
    for(const auto& node : nodes) {
        if(node.isValid && node.nodeNum == from) {
            if(currentTime - node.lastTime < timeout) {
                LOG_DEBUG("Dropping %s packet from 0x%x - too recent", 
                    type == TrackedPacketType::TELEMETRY ? "telemetry" :
                    type == TrackedPacketType::POSITION ? "position" :
                    type == TrackedPacketType::USERINFO ? "user info" : "encrypted", 
                    from);
                return true;
            }
            break;
        }
    }

    updateTrackedNode(from, type);
    return false;
}

void FloodingRouter::updateTrackedNode(NodeNum from, TrackedPacketType type) 
{
    auto& nodes = trackedNodes[static_cast<int>(type)];
    auto& index = trackingIndex[static_cast<int>(type)];
    
    // First try to update existing entry
    for(auto& node : nodes) {
        if(node.isValid && node.nodeNum == from) {
            node.lastTime = millis();
            return;
        }
    }

    // If not found, add to next slot in circular buffer
    nodes[index].nodeNum = from;
    nodes[index].lastTime = millis();
    nodes[index].isValid = true;
    
    // Move to next slot
    index = (index + 1) % MAX_TRACKED_NODES;
}

void FloodingRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    // Check if the sending node is in our ignore list
    if (std::find(ignoredNodes.begin(), ignoredNodes.end(), p->from) != ignoredNodes.end()) {
        LOG_DEBUG("Ignoring rebroadcast from blocked node 0x%08x", p->from);
        return;
    }

    if (!isToUs(p) && (p->hop_limit > 0) && !isFromUs(p)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {

                meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it
                
#if USERPREFS_EVENT_MODE
                if (tosend->hop_limit > 2) {
                    // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                    tosend->hop_start -= (tosend->hop_limit - 2);
                    tosend->hop_limit = 2;
                }
#endif

                tosend->next_hop = NO_NEXT_HOP_PREFERENCE; // this should already be the case, but just in case
                
                if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER || 
                    config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE ||
                    config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) { //check if we are a router

                        // Check for packet type and apply limits
                if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
                    if (shouldDropRecentPacket(p->from, TrackedPacketType::ENCRYPTED)) {
                        return;
                    }
                } else if (p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP) {
                    if (shouldDropRecentPacket(p->from, TrackedPacketType::TELEMETRY)) {
                        return;
                    }
                } else if (p->decoded.portnum == meshtastic_PortNum_POSITION_APP) {
                    if (shouldDropRecentPacket(p->from, TrackedPacketType::POSITION)) {
                        return;
                    }
                } else if (p->decoded.portnum == meshtastic_PortNum_NODEINFO_APP) {
                    if (shouldDropRecentPacket(p->from, TrackedPacketType::USERINFO)) {
                        return;
                    }
                }

                    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                        p->decoded.portnum == meshtastic_PortNum_TELEMETRY_APP) { //check if it is a telemetry packet
                            
                            if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
                                config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
                                LOG_DEBUG("Dropping TELEMETRY_APP (67) from rebroadcast");
                                //return;  //suppress rebroadcast if router or repeater modes, still handled locally
                            }
                            else {
                                if (tosend->hop_limit > 2) { //still rebroadcast but limit telemetry packet hops to 2 if router late mode
                                    tosend->hop_limit = 2;
                                    LOG_DEBUG("Broadcasting Telemetry packet with hop limit 2");
                                }
                                else{
                                    tosend->hop_limit--; //decrement hop limit of telemetry packets if router late mode and hop limit is already 2 or less
                                    LOG_DEBUG("Broadcasting Telemetry packet and decrementing hop limit");
                                }
                            }

                    }                
                    else if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
                        p->decoded.portnum == meshtastic_PortNum_POSITION_APP) {
                        
                        tosend->hop_limit--;//Bump down hop count of postion packets only if we are a router
                        LOG_DEBUG("Decrementing hop count of position packet");
                    }

                    else if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag){ //decrements hop count of encrypted packets
                        tosend->hop_limit--;
                        LOG_DEBUG("Decrementing hop limit of encrypted packet");
                    }

                    else{
                        if (tosend->hop_start < 7){

                            tosend->hop_start++; //Bump up hop start only if we are a router and hop_start - essentially zero hop while still counting hops correctly
                            LOG_DEBUG("Incrementing hop start of non-telemetry/position packet");
                        }
                        else{
                            if (tosend->hop_limit == 7){
                                LOG_DEBUG("Decrementing hop limit to prevent direct node in node list");
                                tosend->hop_limit--;
                            }
                            else{
                                LOG_DEBUG("Zero hop packet");
                            }
                        }
                    }

                }
                else{
                    tosend->hop_limit--;//Bump down hop count only if we are not a router
                }

                if (tosend->decoded.has_bitfield & !(tosend->decoded.bitfield & BITFIELD_OK_TO_MQTT_MASK)) {
                    tosend->decoded.bitfield |= BITFIELD_OK_TO_MQTT_MASK;  // Set the MQTT bit while preserving other bits
                    LOG_DEBUG("Broadcasting with bitfield cleared");
                }

                LOG_INFO("Rebroadcast received floodmsg");
                // Note: we are careful to resend using the original senders node id
                // We are careful not to call our hooked version of send() - because we don't want to check this again
                Router::send(tosend);
            } else {
                LOG_DEBUG("No rebroadcast: Role = CLIENT_MUTE or Rebroadcast Mode = NONE");
            }
        } else {
            LOG_DEBUG("Ignore 0 id broadcast");
        }
    }
}

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
                        (p->decoded.request_id != 0 || p->decoded.reply_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}