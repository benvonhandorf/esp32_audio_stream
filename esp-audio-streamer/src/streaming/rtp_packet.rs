struct RtpPacket {
    version: u8,
    padding: bool,
    extension: bool,
    cc: u8,
    marker: bool,
    pt: u8,
    sequence_number: u16,
    timestamp: u32,
    ssrc: u32,
    csrc_list: Vec<u32>, // Optional
    extensions: Vec<u8>, // Optional
    payload: Vec<u8>,
}