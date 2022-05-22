//! XWOS RUST：线程
//! ========
//!
//! # 线程模型
//!
//! XWOS RUST框架可让用户在Rust的代码中创建XWOS的线程，与C语言的线程一样，每个线程都有独立的名字和栈空间。
//!
//! XWOS RUST框架的线程库是仿照 [`std::thread`] 的来编写的，以降低熟悉Rust的朋友的学习代价。
//!
//! ```rust
//! use xwrust::xwos::thd;
//!
//! thd::spawn(|_| {
//!     // 线程代码;
//! });
//! ```
//!
//! 在上述代码中，线程是自动 **分离的(detached)** ，此线程运行结束后，其资源会被操作系统自动回收。
//!
//! 线程也可以是 **可连接的(joinable)** ，另一线程可以获取创建线程时返回的 [`ThdHandle`] ，
//! 然后通过 [`ThdHandle::join()`] 方法等待线程运行结束：
//!
//! ```rust
//! use xwrust::xwos::thd;
//!
//! let handler = thd::spawn(|_| {
//!     // 线程代码;
//!     // 返回值
//! }).unwrap(); // unwrap()不安全
//!
//! let rc = handler.join().unwrap(); // 等待线程结束，并获取其返回值，unwrap()不安全
//! ```
//!
//! [`ThdHandle::join()`] 方法会返回 [`thd::Result<R>`] ， **R** 是返回值的类型，并放在 [`Ok`] 中。
//!
//! ## 对比 [`std::thread`]
//!
//! #### 闭包原型不同
//!
//! + [`std::thread`] 的闭包原型是 `FnOnce() -> R` ；
//! + `xwrust::xwos::thd` 的闭包原型是
//! `FnOnce(Arc<ThdElement>) -> R` ， [`ThdElement`] 用于存放与线程相关的元素，例如线程的名称。
//! XWOS RUST创建线程时也会创建 [`ThdElement`] ，并用作调用闭包的参数。
//!
//! + [`std::thread`] 通过 `thread::current()` 获取线程自己的`handle`，然后获取线程的名称：
//!
//! ```rust
//! use std::thread;
//!
//! let handler = thread::Builder::new()
//!     .name("named thread".into())
//!     .spawn(|| {
//!         let handle = thread::current();
//!         assert_eq!(handle.name(), Some("named thread"));
//!     }).unwrap();
//!
//! handler.join().unwrap();
//! ```
//!
//! + `xwrust::xwos::thd` 通过闭包参数获取 [`ThdElement`] ，然后通过 [`ele.name()`] 获取线程的名称：
//!
//! ```rust
//! use xwrust::xwos::thd;
//! use libc_print::std_name::println;
//!
//! thd::Builder::new()
//!     .name("foo".into())
//!     .spawn(|ele| {
//!         println!("Thread name: {}", ele.name().unwrap());
//!     });
//! ```
//!
//! [`ThdElement`] 不使用时，使用`_`占位。
//!
//! #### **spawn()** 失败时的处理方式不同
//!
//! + [`std::thread`] **spawn()** 失败时，会直接 [`panic!()`] ；
//! + XWOS是RTOS，常常运行在MCU上， [`panic!()`] 意味着死机，因此需要处理 [`Err`] ；
//!
//! ```rust
//! use xwrust::xwos::thd;
//!
//! match thd::Builder::new()
//!     .name("child".into())
//!     .spawn(|ele| {
//!         // 子线程闭包
//!     }) {
//!         Ok(h) => { // 新建子线程成功
//!         },
//!         Err(rc) => { // 新建子线程失败
//!         },
//!     };
//! ```
//!
//! #### 子线程 [`panic!()`] 的处理方式不同
//!
//! + [`std::thread`] 可捕获子线程的 [`panic!()`] 。
//! + `xwrust::xwos::thd` 的子线程 [`panic!()`] 后会导致整个代码 **halt** 。
//! 目前 **#!\[no_std\]** 环境的 **unwind** 支持还不完善，暂时无法实现类似于 [`std::thread`] 的机制。
//!
//!
//! # 线程的工厂模式
//!
//! 可以通过线程工厂设置线程属性后，再创建线程：
//!
//! ```rust
//! use xwrust::xwos::thd;
//!
//! let builder = thd::Builder::new()
//!                            .name("foo".into()) // 设置线程的名称
//!                            .stack_size(8 * 1024) // 设置线程栈大小
//!                            .privileged(true); // 设置系统权限
//!
//! builder.spawn(|_| {
//!     // 线程代码;
//!     // 返回值
//! });
//! ```
//!
//! ## 线程的名称
//!
//! 线程工厂可以为线程指定一个字符串名字，默认为空。线程的名称可用于日志输出。
//!
//! ## 线程的栈大小
//!
//! 线程工厂可以为线程指定栈内存大小，默认为系统配置 `XWMMCFG_STACK_SIZE_MIN` 。
//!
//! ## 线程的系统权限
//!
//! 某些SOC内部的寄存器，只有拥有系统权限的线程才可以访问，通常这些线程用于提供驱动功能。
//! 默认为有系统权限。
//!
//!
//! # 线程的元素
//!
//! 线程的元素 [`ThdElement`] 是存放与线程相关的私有数据的内存空间，例如线程的名称 。
//!
//!
//! # 线程的句柄
//!
//! 线程的句柄 [`ThdHandle`] 功能类似于 [`std::thread::JoinHandle`] ，可用于结束线程。
//!
//! ## 通知线程退出
//!
//! 方法 [`ThdHandle::quit()`] 可用于父线程通知子线程退出。此方法不会等待子线程退出。
//! 方法 [`ThdHandle::quit()`] 会为子线程设置 **退出状态** ，并中断 **阻塞状态** 和 **睡眠状态** 。
//! 阻塞和睡眠的方法将以返回值负的 [`EINTR`] 退出。错误码 [`EINTR`] 会被转换为各个可阻塞线程的操作系统对象的错误码：
//!
//! + [`MutexError::Interrupt`]
//! + [`SemError::Interrupt`]
//! + [`CondError::Interrupt`]
//!
//! 但是，当子线程的 **阻塞状态** 是不可被中断的，方法 [`ThdHandle::quit()`] 只会为子线程设置 **退出状态** ，不会发生中断。
//!
//! 子线程可以通过 [`cthd::shld_stop()`] 判断是否被设置了 **退出状态** ，可以作为结束线程循环的条件。
//!
//! ## 等待线程退出
//!
//! 当父线程需要等待子线程退出，并捕获其返回值，需要使用方法 [`ThdHandle::join()`] 。
//!
//! + 如果子线程还在运行，此方法会阻塞父线程直到子线程退出。父线程的阻塞状态可被中断；
//! + 如果子线程已经提前运行至退出，此方法可立即返回子线程的返回值。
//!
//! 此方法会消费 [`ThdHandle`] ：
//!
//! + 如果此方法执行成功，会消费掉 [`ThdHandle`] ，并将子线程的返回值放在 [`Ok`] 中返回，因为线程已经结束，其 [`ThdHandle`] 的生命周期也应该结束；
//! + 如果此方法执行失败，会重新在 [`Err`] 中返回 [`ThdHandle`] ，并可通过 [`ThdHandle::join_state()`] 方法获取失败的原因。
//!
//! ## 通知并等待线程退出
//!
//! 方法 [`ThdHandle::stop()`] 等价于 [`ThdHandle::quit()`] + [`ThdHandle::join()`]
//!
//! # 只作用于线程自身的方法
//!
//! XWOS RUST有一组方法，只会对调用线程自身起作用，被实现在 [`xwrust::xwos::cthd`] 。命名中的 **c** 是 **current** 的意思。
//!
//! # 示例
//!
//! [XWOS/xwam/xwrust-example/xwrust_example_thd](https://gitee.com/xwos/XWOS/blob/main/xwam/xwrust-example/xwrust_example_thd/src/lib.rs)
//!
//!
//! [`std::thread`]: <https://doc.rust-lang.org/std/thread/index.html>
//! [`std::thread::JoinHandle`]: <https://doc.rust-lang.org/std/thread/struct.JoinHandle.html>
//! [`panic!()`]: <https://doc.rust-lang.org/std/macro.panic.html>
//! [`thd::Result<R>`]: Result<R>
//! [`Ok`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Ok>
//! [`Err`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Err>
//! [`ele.name()`]: ThdElement::name
//! [`EINTR`]: crate::errno::EINTR
//! [`MutexError::Interrupt`]: crate::xwos::lock::mtx::MutexError::Interrupt
//! [`SemError::Interrupt`]: crate::xwos::sync::sem::SemError::Interrupt
//! [`CondError::Interrupt`]: crate::xwos::sync::cond::CondError::Interrupt
//! [`cthd::shld_stop()`]: crate::xwos::cthd::shld_stop
//! [`xwrust::xwos::cthd`]: crate::xwos::cthd

extern crate core;
use core::ffi::*;
use core::option::Option;
use core::result::Result;
use core::cell::UnsafeCell;
use core::ptr;
use core::mem;
use core::str;
use core::fmt;

extern crate alloc;
use alloc::boxed::Box;
use alloc::string::String;
use alloc::sync::Arc;

use cstr_core::CString;
use cstr_core::CStr;

use crate::types::*;

extern "C" {
    fn xwrustffi_thd_stack_size_default() -> XwSz;
    fn xwrustffi_thd_attr_init(attr: *mut ThdAttr);
    fn xwrustffi_thd_create(thd: *mut *mut c_void, tik: *mut XwSq,
                            attr: *const ThdAttr,
                            mainfunc: extern "C" fn(*mut c_void) -> XwEr,
                            arg: *mut c_void) -> XwEr;
    fn xwrustffi_thd_acquire(thd: *mut c_void, tik: XwSq) -> XwEr;
    fn xwrustffi_thd_release(thd: *mut c_void, tik: XwSq) -> XwEr;
    fn xwrustffi_thd_quit(thd: *mut c_void, tik: XwSq) -> XwEr;
    fn xwrustffi_thd_join(thd: *mut c_void, tik: XwSq, trc: *mut XwEr) -> XwEr;
    fn xwrustffi_thd_stop(thd: *mut c_void, tik: XwSq, trc: *mut XwEr) -> XwEr;
    fn xwrustffi_thd_detach(thd: *mut c_void, tik: XwSq) -> XwEr;
    fn xwrustffi_thd_migrate(thd: *mut c_void, tik: XwSq, cpuid: XwId) -> XwEr;
}

/// XWOS线程的属性
///
/// 完全等价于C语言头文件 `xwos/osal/thd.h` 中的 `struct xwos_thd_attr`
#[repr(C)]
pub(crate) struct ThdAttr {
    /// 线程的名字
    pub(crate) name: *const cty::c_char,
    /// 线程栈的首地址
    pub(crate) stack: *mut XwStk,
    /// 线程栈的大小，以字节(byte)为单位
    pub(crate) stack_size: XwSz,
    /// 栈内存警戒线位置
    pub(crate) stack_guard_size: XwSz,
    /// 优先级
    pub(crate) priority: XwPr,
    /// 是否为分离态
    pub(crate) detached: bool,
    /// 是否为特权线程
    pub(crate) privileged: bool,
}

/// XWOS的线程对象描述符
///
/// 用于调用XWOS-CAPI
pub struct ThdD {
    /// XWOS线程对象的指针
    pub thd: *mut c_void,
    /// XWOS线程对象的标签
    pub tik: XwSq,
}

unsafe impl Send for ThdD {}
unsafe impl Sync for ThdD {}

impl ThdD {
    unsafe fn new(attr: &ThdAttr, func: Box<dyn FnOnce()>) -> Result<ThdD, XwEr> {
        let boxboxfunc = Box::new(func);
        let rawboxfunc = Box::into_raw(boxboxfunc);
        let mut thd: *mut c_void = ptr::null_mut();
        let mut tik: XwSq = 0;
        let rc = xwrustffi_thd_create(&mut thd, &mut tik, attr,
                                      xwrust_thd_entry, rawboxfunc as *mut _);
        return if rc < 0 {
            drop(Box::from_raw(rawboxfunc)); // 创建失败，需要释放f
            Err(rc)
        } else {
            Ok(ThdD {thd: thd, tik: tik})
        };

        extern "C" fn xwrust_thd_entry(main: *mut c_void) -> XwEr {
            unsafe {
                let func = Box::from_raw(main as *mut Box<dyn FnOnce()>);
                func();
            }
            0
        }
    }

    /// 通知线程退出。
    pub fn quit(&self) -> XwEr {
        unsafe {
            xwrustffi_thd_quit(self.thd, self.tik)
        }
    }

    fn join(&mut self) ->XwEr {
        unsafe {
            xwrustffi_thd_join(self.thd, self.tik, ptr::null_mut())
        }
    }

    fn stop(&mut self) -> XwEr {
        unsafe {
            xwrustffi_thd_stop(self.thd, self.tik, ptr::null_mut())
        }
    }

    fn detach(&mut self) -> XwEr {
        unsafe { xwrustffi_thd_detach(self.thd, self.tik) }
    }

    /// 将线程迁移到目标CPU。
    pub fn migrate(&self, cpuid: XwId) -> XwEr {
        unsafe {
            xwrustffi_thd_migrate(self.thd, self.tik, cpuid)
        }
    }
}

impl fmt::Debug for ThdD {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ThdD")
            .field("thd", &self.thd)
            .field("tik", &self.tik)
            .finish()
    }
}

////////////////////////////////////////////////////////////////////////////////
// 工厂模式
////////////////////////////////////////////////////////////////////////////////

/// 线程的工厂模式结构体，可用于配置新线程的属性。
///
/// + [`name`] 设置线程的名字
/// + [`stack_size`] 设置线程的栈大小
/// + [`privileged`] 设置线程的系统权限
///
/// [`spawn`] 方法将获取构建器的所有权，并使用给定的配置创建线程，返回 [`core::result::Result`] 。
///
/// [`thd::spawn`] 独立的函数，并使用默认配置的 `Builder`创建线程，返回 [`core::result::Result`] 。
///
///
/// # 示例
///
/// ```rust
/// use xwrust::xwos::thd;
///
/// let builder = thd::Builder::new();
///
/// builder.spawn(|_| {
///     // 线程代码;
///     // 返回值
/// });
/// ```
///
/// [`name`]: Builder::name
/// [`stack_size`]: Builder::stack_size
/// [`privileged`]: Builder::privileged
/// [`spawn`]: Builder::spawn
/// [`thd::spawn`]: spawn
/// [`core::result::Result`]: <https://doc.rust-lang.org/core/result/enum.Result.html>
pub struct Builder {
    /// 线程的名字
    name: Option<String>,
    /// 线程栈的大小，以字节(byte)为单位
    stack_size: Option<usize>,
    /// 是否为特权线程
    privileged: Option<bool>,
}

impl Builder {
    /// 新建用于创建线程的工厂。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// let builder = thd::Builder::new()
    ///                            .name("foo".into()) // 设置线程的名称
    ///                            .stack_size(8 * 1024) // 设置线程栈大小
    ///                            .privileged(true); // 设置系统权限
    ///
    /// builder.spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// });
    /// ```
    pub fn new() -> Builder {
        Builder {
            name: None,
            stack_size: None,
            privileged: None,
        }
    }

    /// 设置线程的名称。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    /// use libc_print::std_name::println;
    ///
    /// let builder = thd::Builder::new()
    ///                            .name("foo".into()); // 设置线程的名称
    ///
    /// builder.spawn(|ele| {
    ///     println!("My name is {}.", ele.name().unwrap());
    ///     // 线程代码;
    ///     // 返回值
    /// });
    /// ```
    pub fn name(mut self, name: String) -> Builder {
        self.name = Some(name);
        self
    }

    /// 设置线程栈的大小。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// let builder = thd::Builder::new()
    ///                            .stack_size(8 * 1024); // 设置线程栈大小
    ///
    /// builder.spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// });
    /// ```
    pub fn stack_size(mut self, size: usize) -> Builder {
        self.stack_size = Some(size);
        self
    }

    /// 设置线程栈的系统权限。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// let builder = thd::Builder::new()
    ///                            .privileged(true); // 设置系统权限
    ///
    /// builder.spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// });
    /// ```
    pub fn privileged(mut self, privileged: bool) -> Builder {
        self.privileged = Some(privileged);
        self
    }

    /// 转移 `Builder` 的所有权，并新建一个线程：
    ///
    /// + 创建线程成功，返回一个包含 [`ThdHandle`] 的 [`core::result::Result`] ；
    /// + 创建线程失败，返回一个包含 [`XwEr`] 的 [`core::result::Result`] ， [`XwEr`] 指示错误的原因。
    ///
    /// 方法的签名：
    ///
    /// + [`'static`] 约束是因为新建的线程可能比调用者的生命周期更长，因此线程的闭包和返回值的生命周期限定为静态生命周期；
    /// + [`Send`] 约束是因为闭包和返回值需要在线程之间进行转移。并且被移动到 [`Send`] 约束的闭包的变量也必须是 [`Send`] 的，否则编译器会报错。
    ///   RUSTC是通过 [`Send`] 约束来区分闭包是不是另一线程的代码的。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// let builder = thd::Builder::new()
    ///     .name("foo".into()) // 设置线程的名称
    ///     .stack_size(8 * 1024) // 设置线程栈大小
    ///     .privileged(true); // 设置系统权限
    /// match builder.spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// }) {
    ///     Ok(handler) => {
    ///         match handler.join() {
    ///             Ok(r) => {
    ///                 // r 是线程闭包的返回值。
    ///             },
    ///             Err(e) => {
    ///                 // join() 失败的错误码可通过 e.join_state() 获取。
    ///                 // e 是 ThdHandle<R> ，重新被返回。
    ///             },
    ///         }
    ///     },
    ///     Err(rc) => {
    ///         // rc 是 spawn() 失败时的错误码。
    ///     }
    /// };
    /// ```
    /// [`core::result::Result`]: <https://doc.rust-lang.org/core/result/enum.Result.html>
    /// [`'static`]: <https://doc.rust-lang.org/std/keyword.static.html>
    /// [`Send`]: <https://doc.rust-lang.org/core/marker/trait.Send.html>
    pub fn spawn<F, R>(self, f: F) -> Result<ThdHandle<R>, XwEr>
    where
        F: FnOnce(Arc<ThdElement>) -> R,
        F: Send + 'static,
        R: Send + 'static,
    {
        unsafe { self.spawn_unchecked(f) }
    }

    /// 转移 `Builder` 的所有权，并产生一个新线程：
    ///
    /// + 创建线程成功，返回一个包含 [`ThdHandle`] 的 [`core::result::Result`] ；
    /// + 创建线程失败，返回一个包含 [`XwEr`] 的 [`core::result::Result`] ， [`XwEr`] 指示错误的原因。
    ///
    /// 此方法只要求闭包 `F` 和 返回值 `R` 的生命周期一样长，然后不做其他限制，因此是 `unsafe` 的。
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// let builder = thd::Builder::new()
    ///     .name("foo".into()) // 设置线程的名称
    ///     .stack_size(8 * 1024) // 设置线程栈大小
    ///     .privileged(true); // 设置系统权限
    /// match unsafe { builder.spawn_unchecked(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// })} {
    ///     Ok(handler) => {
    ///         match handler.join() {
    ///             Ok(r) => {
    ///                 // r 是线程闭包的返回值。
    ///             },
    ///             Err(e) => {
    ///                 // join() 失败时的错误码可通过 e.join_state() 获取。
    ///                 // e 是 ThdHandle<R> ，重新被返回。
    ///             },
    ///         }
    ///     },
    ///     Err(rc) => {
    ///         // rc 是 spawn() 失败时的错误码。
    ///     }
    /// };
    /// ```
    /// [`core::result::Result`]: <https://doc.rust-lang.org/core/result/enum.Result.html>
    pub unsafe fn spawn_unchecked<'a, F, R>(self, f: F) -> Result<ThdHandle<R>, XwEr>
    where
        F: FnOnce(Arc<ThdElement>) -> R,
        F: Send + 'a,
        R: Send + 'a,
    {
        Ok(ThdHandle{inner: self.spawn_unchecked_(f)?})
    }

    unsafe fn spawn_unchecked_<'a, F, R>(self, f: F) -> Result<ThdHandleInner<R>, XwEr>
    where
        F: FnOnce(Arc<ThdElement>) -> R,
        F: Send + 'a,
        R: Send + 'a,
    {
        let mut attr: ThdAttr = mem::zeroed();
        xwrustffi_thd_attr_init(&mut attr);
        attr.privileged = self.privileged.unwrap_or(true);
        attr.stack_size = self.stack_size.unwrap_or(xwrustffi_thd_stack_size_default());
        let element: Arc<ThdElement> =
            Arc::new(ThdElement::new(self.name.map(|name| {
            match CString::new(name) {
                Ok(n) => n,
                Err(_e) => {
                    CString::new("InvalidName").unwrap()
                },
            }
            })));
        let thd_element = element.clone();
        attr.name = match element.cname() {
            Some(s) => s.as_ptr(),
            None => ptr::null(),
        };

        let retval: Arc<ThdReturnValue<R>> =
            Arc::new(ThdReturnValue { result: UnsafeCell::new(None) });
        let thd_retval = retval.clone();

        let main = move || {
            let result = f(thd_element);
            *thd_retval.result.get() = Some(result);
        };

        Ok(ThdHandleInner {
            thdd: ThdD::new(&attr,
                            mem::transmute::<Box<dyn FnOnce() + 'a>, Box<dyn FnOnce() + 'static>>(Box::new(main)))?,
            join_state: STATE_JOINABLE,
            element: element,
            rv: retval,
        })
    }
}

/// 新建一个线程。
///
/// + 创建线程成功，返回一个包含 [`ThdHandle`] 的 [`core::result::Result`] ；
/// + 创建线程失败，返回一个包含 [`XwEr`] 的 [`core::result::Result`] ， [`XwEr`] 指示错误的原因。
///
/// 此方法使用默认的线程工厂创建线程。
///
/// 当 [`ThdHandle`] 被 [`drop()`] 时，新建的线程会变成 **detached（分离的）** 。此时，新建的线程不能再被 [`join()`] 。
///
/// 方法的签名：
///
/// + [`'static`] 约束是因为新建的线程可能比调用者的生命周期更长，因此线程的闭包和返回值的生命周期限定为静态生命周期；
/// + [`Send`] 约束是因为闭包和返回值需要在线程之间进行转移。并且被移动到 [`Send`] 约束的闭包的变量也必须是 [`Send`] 的，否则编译器会报错。
///   RUSTC是通过 [`Send`] 约束来区分闭包是不是另一线程的代码的。
///
/// # 示例
///
/// ```rust
/// use xwrust::xwos::thd;
///
/// match thd::spawn(|_| {
///     // 线程代码;
///     // 返回值
/// }) {
///     Ok(handler) => {
///         match handler.join() {
///             Ok(r) => {
///                 // r 是线程闭包的返回值。
///             },
///             Err(e) => {
///                 // join() 失败时的错误码可通过 e.join_state() 获取。
///                 // e 是 ThdHandle<R> ，重新被返回。
///             },
///         }
///     },
///     Err(rc) => {
///         // rc 是 spawn() 失败时的错误码。
///     },
/// };
/// ```
/// [`core::result::Result`]: <https://doc.rust-lang.org/core/result/enum.Result.html>
/// [`drop()`]: <https://doc.rust-lang.org/core/ops/trait.Drop.html#tymethod.drop>
/// [`join()`]: ThdHandle::join
/// [`'static`]: <https://doc.rust-lang.org/std/keyword.static.html>
/// [`Send`]: <https://doc.rust-lang.org/core/marker/trait.Send.html>
pub fn spawn<F, R>(f: F) -> Result<ThdHandle<R>, XwEr>
where
    F: FnOnce(Arc<ThdElement>) -> R,
    F: Send + 'static,
    R: Send + 'static,
{
    Builder::new().spawn(f)
}

/// 线程的元素
///
/// 线程的元素中的数据需跨线程共享，因此在定义时需要使用 [`Arc`] 进行封装。
///
/// [`Arc`]: <https://doc.rust-lang.org/alloc/sync/struct.Arc.html>
pub struct ThdElement {
    /// 线程的名称
    name: Option<CString>,
}

impl ThdElement {
    pub(crate) fn new(name: Option<CString>) -> ThdElement {
        ThdElement { name }
    }

    /// 返回名字字符串的引用
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    /// use libc_print::std_name::println;
    ///
    /// let handler = thd::Builder::new()
    ///     .name("foo".into())
    ///     .spawn(|ele| {
    ///         println!("Thread name: {}", ele.name().unwrap());
    ///     });
    /// ```
    pub fn name(&self) -> Option<&str> {
        self.cname().map(|s| unsafe { str::from_utf8_unchecked(s.to_bytes()) })
    }

    fn cname(&self) -> Option<&CStr> {
        self.name.as_deref()
    }
}

impl fmt::Debug for ThdElement {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ThdElement")
            .field("name", &self.name)
            .finish()
    }
}

/// 线程的返回值
///
/// 线程的返回值中的数据需跨线程共享，因此在定义时需要使用 [`Arc`] 进行封装。
///
/// [`Arc`]: <https://doc.rust-lang.org/alloc/sync/struct.Arc.html>
struct ThdReturnValue<R> {
    result: UnsafeCell<Option<R>>,
}

unsafe impl<R: Sync> Sync for ThdReturnValue<R> {}

pub const STATE_JOINED: XwEr = 0;
pub const STATE_JOINABLE: XwEr = 1;

struct ThdHandleInner<R> {
    /// XWOS线程的描述符
    thdd: ThdD,
    /// join()/stop()失败时返回值
    join_state: XwEr,
    /// 线程的元素
    element: Arc<ThdElement>,
    /// 线程的返回值
    rv: Arc<ThdReturnValue<R>>,
}

impl<R> ThdHandleInner<R> {
    fn quit(&self) -> XwEr {
        self.thdd.quit()
    }

    fn join(mut self) -> Result<R, Self> {
        let rc = self.thdd.join();
        if rc == 0 {
            self.join_state = STATE_JOINED;
            Ok(Arc::get_mut(&mut self.rv).unwrap().result.get_mut().take().unwrap())
        } else {
            self.join_state = rc;
            Err(self)
        }
    }

    fn stop(mut self) -> Result<R, Self> {
        let rc = self.thdd.stop();
        if rc == 0 {
            self.join_state = STATE_JOINED;
            Ok(Arc::get_mut(&mut self.rv).unwrap().result.get_mut().take().unwrap())
        } else {
            self.join_state = rc;
            Err(self)
        }
    }
}

impl<R> Drop for ThdHandleInner<R> {
    fn drop(&mut self) {
        if self.join_state != STATE_JOINED {
            self.thdd.detach();
        }
    }
}

impl<R> fmt::Debug for ThdHandleInner<R> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ThdHandleInner")
            .field("thdd", &self.thdd)
            .field("join_state", &self.join_state)
            .field("element", &self.element)
            .finish_non_exhaustive()
    }
}

/// 线程的句柄
pub struct ThdHandle<R> {
    inner: ThdHandleInner<R>
}

unsafe impl<R> Send for ThdHandle<R> {}
unsafe impl<R> Sync for ThdHandle<R> {}

impl<R> ThdHandle<R> {
    /// 返回XWOS的线程对象描述符。线程对象描述符用于与C语言交互。
    pub fn thdd(&self) -> &ThdD {
        &self.inner.thdd
    }

    /// 返回线程的元素。
    pub fn element(&self) -> &ThdElement {
        &self.inner.element
    }


    /// 通知线程退出。
    ///
    /// 此方法用于向线程设置 **退出状态** 。
    ///
    /// 调用此方法的线程不会等待被设置 **退出状态** 的线程退出。
    ///
    /// 此方法可被重复调用，线程的 **退出状态** 一旦被设置，不可被清除。
    ///
    /// # 上下文
    ///
    /// + 线程、中断、中断底半部、空闲任务
    ///
    pub fn quit(&self) -> XwEr {
        self.inner.quit()
    }


    /// 等待线程运行至退出，并返回线程的返回值。
    ///
    /// + 如果子线程还在运行，此方法会阻塞父线程直到子线程退出。父线程的阻塞状态可被中断；
    /// + 如果子线程已经提前运行至退出，此方法可立即返回子线程的返回值。
    ///
    /// 此方法会消费 [`self`] ：
    ///
    /// + 如果此方法执行成功，会消费掉 [`self`] ，并将子线程的返回值放在 [`Ok`] 中返回，因为线程已经结束，其 [`ThdHandle`] 的生命周期也应该结束；
    /// + 如果此方法执行失败，会重新在 [`Err`] 中返回 [`self`] ，并可通过 [`ThdHandle::join_state()`] 方法获取失败的原因。
    ///
    /// # 上下文
    ///
    /// + 线程
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// match thd::spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// }) {
    ///     Ok(handler) => {
    ///         match handler.join() {
    ///             Ok(r) => {
    ///                 // r 是线程闭包的返回值。
    ///             },
    ///             Err(e) => {
    ///                 // join() 失败时的错误码可通过 e.join_state() 获取。
    ///                 // e 是 ThdHandle<R> ，重新被返回。
    ///             },
    ///         }
    ///     },
    ///     Err(rc) => {
    ///         // rc 是 spawn() 失败时的错误码。
    ///     },
    /// };
    /// ```
    ///
    /// [`Ok`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Ok>
    /// [`Err`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Err>
    pub fn join(self) -> Result<R, Self> {
        match self.inner.join() {
            Ok(r) => Ok(r),
            Err(e) => Err(Self{inner: e}),
        }
    }

    /// 终止线程并等待线程运行至退出，并返回线程的返回值。
    ///
    /// + 如果子线程还在运行，此方法会阻塞父线程直到子线程退出。父线程的阻塞状态可被中断；
    /// + 如果子线程已经提前运行至退出，此方法可立即返回子线程的返回值。
    ///
    /// 此方法 = [`ThdHandle::quit()`] + [`ThdHandle::join()`]
    ///
    ///
    /// 此方法会消费 [`self`] ：
    ///
    /// + 如果此方法执行成功，会消费掉 [`self`] ，并将子线程的返回值放在 [`Ok`] 中返回，因为线程已经结束，其 [`ThdHandle`] 的生命周期也应该结束；
    /// + 如果此方法执行失败，会重新在 [`Err`] 中返回 [`self`] ，并可通过 [`ThdHandle::join_state()`] 方法获取失败的原因。
    ///
    /// # 上下文
    ///
    /// + 线程
    ///
    /// # 示例
    ///
    /// ```rust
    /// use xwrust::xwos::thd;
    ///
    /// match thd::spawn(|_| {
    ///     // 线程代码;
    ///     // 返回值
    /// }) {
    ///     Ok(handler) => {
    ///         match handler.stop() {
    ///             Ok(r) => {
    ///                 // r 是线程闭包的返回值。
    ///             },
    ///             Err(e) => {
    ///                 // stop() 失败时的错误码可通过 e.join_state() 获取。
    ///                 // e 是 ThdHandle<R> ，重新被返回。
    ///             },
    ///         }
    ///     },
    ///     Err(rc) => {
    ///         // rc 是 spawn() 失败时的错误码。
    ///     },
    /// };
    /// ```
    ///
    /// [`Ok`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Ok>
    /// [`Err`]: <https://doc.rust-lang.org/core/result/enum.Result.html#variant.Err>
    pub fn stop(self) -> Result<R, Self> {
        match self.inner.stop() {
            Ok(r) => Ok(r),
            Err(e) => Err(Self{inner: e}),
        }
    }

    /// 返回join()失败时的错误码。
    pub fn join_state(&self) -> XwEr {
        self.inner.join_state
    }

    /// 检查关联的线程是否运行结束。
    ///
    /// 此方法不会阻塞调用者。
    pub fn finished(&self) -> bool {
        Arc::strong_count(&self.inner.rv) == 1
    }
}

impl<R> fmt::Debug for ThdHandle<R> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("ThdHandle")
            .field("inner", &self.inner)
            .finish_non_exhaustive()
    }
}
