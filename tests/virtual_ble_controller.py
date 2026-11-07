"""用于测试的虚拟加密控制器，验证双方LTK相同后才报告加密成功。"""
import hmac
import struct
from bumble import hci
from bumble.controller import Controller


class CheckedController(Controller):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.pending_encryption = {}
        self.encryption_checks = 0
        self.unsupported = set()
        self.encrypted_handles = set()

    def on_link_disconnected(self, peer_address, reason):
        connection = self.find_le_connection_by_address(peer_address)
        if connection:
            self.pending_encryption.pop(connection.handle, None)
            self.encrypted_handles.discard(connection.handle)
        return super().on_link_disconnected(peer_address, reason)

    def on_link_disconnection_complete(self, command, status):
        self.pending_encryption.pop(command.connection_handle, None)
        self.encrypted_handles.discard(command.connection_handle)
        return super().on_link_disconnection_complete(command, status)

    def on_hci_reset_command(self, command):
        self.pending_encryption.clear()
        self.encrypted_handles.clear()
        return super().on_hci_reset_command(command)

    def on_hci_command(self, command):
        self.unsupported.add(command.name)
        return super().on_hci_command(command)

    def on_hci_le_enable_encryption_command(self, command):
        connection = self.find_central_connection_by_handle(command.connection_handle)
        if connection is None or self.link is None:
            return bytes([hci.HCI_INVALID_HCI_COMMAND_PARAMETERS_ERROR])
        peer = self.link.find_controller(connection.peer_address)
        remote = peer.find_le_connection_by_address(self.random_address) if peer else None
        if remote is None or not isinstance(peer, CheckedController):
            return bytes([hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR])
        if remote.handle in peer.pending_encryption:
            return bytes([hci.HCI_COMMAND_DISALLOWED_ERROR])
        self.send_hci_packet(hci.HCI_Command_Status_Event(status=0, num_hci_command_packets=1, command_opcode=command.op_code))
        peer.pending_encryption[remote.handle] = (self, connection.handle, bytes(command.long_term_key))
        peer.send_hci_packet(hci.HCI_LE_Long_Term_Key_Request_Event(connection_handle=remote.handle, random_number=command.random_number, encryption_diversifier=command.encrypted_diversifier))
        return None

    def complete_encryption(self, handle, key):
        pending = self.pending_encryption.pop(handle, None)
        if pending is None:
            return hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR
        central, central_handle, expected = pending
        valid = key is not None and len(expected) == 16 and len(key) == 16 and hmac.compare_digest(expected, bytes(key))
        status = 0 if valid else hci.HCI_AUTHENTICATION_FAILURE_ERROR
        for controller, connection_handle in ((central, central_handle), (self, handle)):
            if valid:
                controller.encrypted_handles.add(connection_handle)
            else:
                controller.encrypted_handles.discard(connection_handle)
            controller.send_hci_packet(hci.HCI_Encryption_Change_Event(status=status, connection_handle=connection_handle, encryption_enabled=int(valid)))
        if valid:
            self.encryption_checks += 1
            central.encryption_checks += 1
        return 0

    def on_hci_le_long_term_key_request_reply_command(self, command):
        status = self.complete_encryption(command.connection_handle, command.long_term_key)
        return struct.pack('<BH', status, command.connection_handle)

    def on_hci_le_long_term_key_request_negative_reply_command(self, command):
        status = self.complete_encryption(command.connection_handle, None)
        return struct.pack('<BH', status, command.connection_handle)

    def on_hci_read_encryption_key_size_command(self, command):
        connection = self.find_central_connection_by_handle(command.connection_handle) or self.find_peripheral_connection_by_handle(command.connection_handle)
        status = (hci.HCI_UNKNOWN_CONNECTION_IDENTIFIER_ERROR if connection is None else
                  0 if command.connection_handle in self.encrypted_handles else hci.HCI_INSUFFICIENT_SECURITY_ERROR)
        return struct.pack('<BHB', status, command.connection_handle, 16 if status == 0 else 0)
