use std::{
    ffi::{c_ulong, c_ushort},
    marker::PhantomData,
    mem::MaybeUninit,
    net::{IpAddr, Ipv4Addr, SocketAddr},
    os::windows::prelude::{AsRawSocket, FromRawSocket},
};
use windows_sys::Win32::Networking::WinSock;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum GeneralTimestampMode {
    SoftwareAll,
    SoftwareRecv,
    #[default]
    None,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Default)]
pub enum InterfaceTimestampMode {
    HardwareAll,
    HardwareRecv,
    HardwarePTPAll,
    HardwarePTPRecv,
    SoftwareAll,
    SoftwareRecv,
    #[default]
    None,
}

/// Turn a C failure (-1 is returned) into a rust Result
pub(crate) fn cerr(t: libc::c_int) -> std::io::Result<libc::c_int> {
    match t {
        -1 => Err(std::io::Error::last_os_error()),
        _ => Ok(t),
    }
}

#[derive(Debug)]
struct RawSocket {
    socket: socket2::Socket,
}

impl RawSocket {
    pub fn new() -> std::io::Result<Self> {
        use socket2::{Domain, Protocol, Socket, Type};

        let socket = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP)).unwrap();

        Ok(RawSocket { socket })
    }

    pub(crate) fn bind(&self, addr: SocketAddr) -> std::io::Result<()> {
        self.socket.bind(&addr.into())
    }

    pub(crate) fn set_nonblocking(&self, nonblocking: bool) -> std::io::Result<()> {
        self.socket.set_nonblocking(nonblocking)
        //        let mut nonblocking = nonblocking as libc::c_uint;
        //        cerr(unsafe { WinSock::ioctlsocket(self.fd as _, WinSock::FIONBIO, &mut nonblocking) })
        //            .map(drop)
    }

    pub(crate) fn connect(&self, addr: SocketAddr) -> std::io::Result<()> {
        //        let len = std::mem::size_of_val(&addr) as i32;
        //
        //        // Safety: socket is valid for the duration of the call, addr lives for the duration of
        //        // the call and len is at most the length of addr.
        //        cerr(unsafe { libc::connect(self.fd as _, &addr as *const _ as *const _, len) })?;
        //        Ok(())
        self.socket.connect(&addr.into())
    }
}

#[derive(Debug)]
struct AsyncFd<T> {
    handle: T,
}

impl<T> AsyncFd<T> {
    fn new(handle: T) -> std::io::Result<Self> {
        Ok(Self { handle })
    }

    fn get_ref(&self) -> &T {
        &self.handle
    }
}

#[derive(Debug)]
pub struct Socket<A, S> {
    timestamp_mode: InterfaceTimestampMode,
    socket: AsyncFd<RawSocket>,
    #[cfg(target_os = "linux")]
    send_counter: u32,
    _addr: PhantomData<A>,
    _state: PhantomData<S>,
}

#[derive(Debug)]
pub struct Open;
#[derive(Debug)]
pub struct Connected;

fn to_sockaddr(addr: SocketAddr) -> libc::sockaddr {
    match addr {
        SocketAddr::V4(addr) => {
            assert!(addr.ip() == &Ipv4Addr::LOCALHOST);

            let foo = WinSock::SOCKADDR_IN {
                sin_family: WinSock::AF_INET,
                sin_port: addr.port(),
                sin_addr: unsafe { std::mem::transmute::<_, _>([0u8; 4]) },
                sin_zero: [0; 8],
            };

            unsafe { std::mem::transmute::<_, libc::sockaddr>(foo) }
        }
        SocketAddr::V6(addr) => {
            todo!();
        }
    }
}

#[repr(C)]
struct TIMESTAMPING_CONFIG {
    Flags: c_ulong,
    TxTimestampsBuffered: c_ushort,
}

fn configure_timestamping(
    _socket: &RawSocket,
    mode: InterfaceTimestampMode,
) -> std::io::Result<()> {
    match mode {
        InterfaceTimestampMode::None => Ok(()),
        InterfaceTimestampMode::SoftwareAll => todo!(),
        InterfaceTimestampMode::SoftwareRecv => {
            let fd = _socket.socket.as_raw_socket();

            let mut config = WinSock::TIMESTAMPING_CONFIG {
                Flags: WinSock::TIMESTAMPING_FLAG_RX,
                TxTimestampsBuffered: 0,
            };

            cerr(unsafe {
                WinSock::ioctlsocket(
                    fd as _,
                    WinSock::SIO_TIMESTAMPING as _,
                    (&mut config) as *mut _ as *mut u32,
                )
            })
            .map(drop)
        }
        _ => Err(std::io::ErrorKind::Unsupported.into()),
    }
}

impl From<GeneralTimestampMode> for InterfaceTimestampMode {
    fn from(value: GeneralTimestampMode) -> Self {
        match value {
            GeneralTimestampMode::SoftwareAll => InterfaceTimestampMode::SoftwareAll,
            GeneralTimestampMode::SoftwareRecv => InterfaceTimestampMode::SoftwareRecv,
            GeneralTimestampMode::None => InterfaceTimestampMode::None,
        }
    }
}

impl Socket<SocketAddr, Open> {
    pub async fn send_to(
        &mut self,
        buf: &[u8],
        local_addr: SocketAddr,
        remote_addr: SocketAddr,
    ) -> std::io::Result<Option<Timestamp>> {
        let mut socket = RawSocket::new()?;
        std::mem::swap(&mut self.socket.handle, &mut socket);

        let std_socket = socket.socket.into();
        let tokio_socket = tokio::net::UdpSocket::from_std(std_socket).unwrap();

        tokio_socket.send_to(buf, remote_addr).await?;

        let mut socket = RawSocket {
            socket: tokio_socket.into_std().unwrap().into(),
        };
        std::mem::swap(&mut self.socket.handle, &mut socket);

        if matches!(
            self.timestamp_mode,
            InterfaceTimestampMode::HardwarePTPAll | InterfaceTimestampMode::SoftwareAll
        ) {
            #[cfg(target_os = "linux")]
            {
                let expected_counter = self.send_counter;
                self.send_counter = self.send_counter.wrapping_add(1);
                self.fetch_send_timestamp(expected_counter).await
            }

            #[cfg(not(target_os = "linux"))]
            {
                unreachable!("Should not be able to create send timestamping sockets on platforms other than linux")
            }
        } else {
            Ok(None)
        }
    }

    pub fn connect(self, addr: SocketAddr) -> std::io::Result<Socket<SocketAddr, Connected>> {
        self.socket.get_ref().connect(addr)?;
        Ok(Socket {
            timestamp_mode: self.timestamp_mode,
            socket: self.socket,
            #[cfg(target_os = "linux")]
            send_counter: self.send_counter,
            _addr: PhantomData,
            _state: PhantomData,
        })
    }
}

pub struct RecvResult<A> {
    pub bytes_read: usize,
    pub remote_addr: A,
    pub timestamp: Option<Timestamp>,
}

impl<S> Socket<SocketAddr, S> {
    pub async fn recv(&mut self, buf: &mut [u8]) -> std::io::Result<RecvResult<SocketAddr>> {
        let mut socket = RawSocket::new()?;
        std::mem::swap(&mut self.socket.handle, &mut socket);

        let std_socket = socket.socket.into();
        let tokio_socket = tokio::net::UdpSocket::from_std(std_socket).unwrap();

        let (bytes_read, remote_addr) = tokio_socket.recv_from(buf).await?;

        let mut socket = RawSocket {
            socket: tokio_socket.into_std().unwrap().into(),
        };
        std::mem::swap(&mut self.socket.handle, &mut socket);

        Ok(RecvResult {
            bytes_read,
            remote_addr,
            timestamp: None,
        })
    }
}

pub fn open_ip(
    addr: SocketAddr,
    timestamping: GeneralTimestampMode,
) -> std::io::Result<Socket<SocketAddr, Open>> {
    // Setup the socket
    let socket = match addr {
        SocketAddr::V4(_) => RawSocket::new(),
        SocketAddr::V6(_) => todo!(),
    }?;

    socket.bind(addr)?;
    socket.set_nonblocking(true)?;
    configure_timestamping(&socket, timestamping.into())?;

    Ok(Socket {
        timestamp_mode: timestamping.into(),
        socket: AsyncFd::new(socket)?,
        #[cfg(target_os = "linux")]
        send_counter: 0,
        _addr: PhantomData,
        _state: PhantomData,
    })
}

pub fn connect_address(
    addr: SocketAddr,
    timestamping: GeneralTimestampMode,
) -> std::io::Result<Socket<SocketAddr, Connected>> {
    // Setup the socket
    let socket = match addr {
        SocketAddr::V4(_) => RawSocket::new(),
        SocketAddr::V6(_) => todo!(),
    }?;

    socket.connect(addr)?;
    socket.set_nonblocking(true)?;
    configure_timestamping(&socket, timestamping.into())?;

    Ok(Socket {
        timestamp_mode: timestamping.into(),
        socket: AsyncFd::new(socket)?,
        #[cfg(target_os = "linux")]
        send_counter: 0,
        _addr: PhantomData,
        _state: PhantomData,
    })
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, PartialOrd, Ord, Hash, Default)]
pub struct Timestamp {
    pub seconds: i64,
    pub nanos: u32,
}

impl Socket<SocketAddr, Connected> {
    pub async fn send(&mut self, buf: &[u8]) -> std::io::Result<Option<Timestamp>> {
        // self.socket .async_io(Interest::WRITABLE, |socket| socket.send(buf)) .await?;

        let mut socket = RawSocket::new()?;
        std::mem::swap(&mut self.socket.handle, &mut socket);

        let std_socket = socket.socket.into();
        let tokio_socket = tokio::net::UdpSocket::from_std(std_socket).unwrap();

        tokio_socket.send(buf).await?;

        let mut socket = RawSocket {
            socket: tokio_socket.into_std().unwrap().into(),
        };
        std::mem::swap(&mut self.socket.handle, &mut socket);

        if matches!(
            self.timestamp_mode,
            InterfaceTimestampMode::HardwarePTPAll | InterfaceTimestampMode::SoftwareAll
        ) {
            #[cfg(target_os = "linux")]
            {
                let expected_counter = self.send_counter;
                self.send_counter = self.send_counter.wrapping_add(1);
                self.fetch_send_timestamp(expected_counter).await
            }

            #[cfg(not(target_os = "linux"))]
            {
                unreachable!("Should not be able to create send timestamping sockets on platforms other than linux")
            }
        } else {
            Ok(None)
        }
    }
}

#[tokio::main(flavor = "current_thread")]
async fn main() {
    use std::net::{IpAddr, Ipv4Addr};

    let mut data: WinSock::WSADATA = unsafe { MaybeUninit::zeroed().assume_init() };
    unsafe { WinSock::WSAStartup(u16::from_be_bytes([2, 2]), &mut data) };

    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 5125);
    let mut a = open_ip(addr, GeneralTimestampMode::SoftwareRecv).unwrap();
    let mut b = connect_address(addr, GeneralTimestampMode::None).unwrap();

    assert!(b.send(&[1, 2, 3]).await.is_ok());
    let mut buf = [0; 4];
    let recv_result = a.recv(&mut buf).await.unwrap();
    assert_eq!(recv_result.bytes_read, 3);
    assert_eq!(&buf[0..3], &[1, 2, 3]);

    println!("before");
    dbg!(recv_result.remote_addr);
    assert!(a
        .send_to(&[4, 5, 6], addr, recv_result.remote_addr)
        .await
        .is_ok());
    println!("after");
    let recv_result = b.recv(&mut buf).await.unwrap();
    assert_eq!(recv_result.bytes_read, 3);
    assert_eq!(&buf[0..3], &[4, 5, 6]);

    /*
    use socket2::{Domain, Protocol, Socket, Type};
    use std::net::{SocketAddr, TcpListener};

    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 5125);

    let a = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP)).unwrap();
    a.set_nonblocking(true).unwrap();
    a.bind(&addr.into()).unwrap();

    let b = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP)).unwrap();
    b.set_nonblocking(true).unwrap();
    b.connect(&addr.into()).unwrap();

    assert!(b.send(&[1, 2, 3]).is_ok());
    let mut buf = [MaybeUninit::new(0); 4];
    let recv_result = a.recv(&mut buf).unwrap();
    // assert_eq!(recv_result.bytes_read, 3);
    let buf = buf.map(|x| unsafe { x.assume_init() });
    assert_eq!(&buf[0..3], &[1, 2, 3]);

    assert!(a.send_to(&[4, 5, 6], &b.local_addr().unwrap()).is_ok());
    println!("after");
    let mut buf = [MaybeUninit::new(0); 4];
    let recv_result = b.recv(&mut buf).unwrap();
    // assert_eq!(recv_result.bytes_read, 3);
    let buf = buf.map(|x| unsafe { x.assume_init() });
    assert_eq!(&buf[0..3], &[4, 5, 6]);
    */

    println!("all done");
}
