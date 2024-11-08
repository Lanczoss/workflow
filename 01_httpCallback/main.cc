// http客户端
#include <signal.h>
#include <workflow/HttpUtil.h>
#include <workflow/WFFacilities.h>
#include <workflow/WFTaskFactory.h>

#include <iostream>
using std::cerr;
using std::cout;
static WFFacilities::WaitGroup waitGroup(1);  // 让主线程阻塞用的对象
void sighandler(int signum) {                 // 信号处理函数
  waitGroup.done();  // 遇到2号信号时，执行一次done()可以主线程结束
  cout << "done\n";
}

// http任务的回调函数，传入的是http任务参数
void httpCallback(WFHttpTask *task) {
  // 拿到请求信息
  protocol::HttpRequest *req = task->get_req();
  // 拿到响应信息
  protocol::HttpResponse *resp = task->get_resp();
  // 拿到状态信息
  int state = task->get_state();
  // 拿到错误信息
  int error = task->get_error();

  // 根据状态信息判断是否有错误
  switch (state) {
    // 系统错误
    case WFT_STATE_SYS_ERROR:
      cerr << "system error: " << strerror(error) << "\n";
      break;
    // DNS错误
    case WFT_STATE_DNS_ERROR:
      cerr << "DNS error: " << gai_strerror(error) << "\n";
      break;
    // SSL错误
    case WFT_STATE_SSL_ERROR:
      cerr << "SSL error: " << error << "\n";
      break;
    // 任务错误
    case WFT_STATE_TASK_ERROR:
      cerr << "Task error: " << error << "\n";
      break;
    // 状态成功
    case WFT_STATE_SUCCESS:
      break;
  }
  // 如果状态不是成功
  // 打印错误信息
  if (state != WFT_STATE_SUCCESS) {
    cerr << "Failed. Press Ctrl-C to exit.\n";
    return;
  }

  // 打印请求行的方法，版本，路径和参数
  cerr << req->get_method() << " " << req->get_http_version() << " "
       << req->get_request_uri() << "\n";

  // 用于遍历首部字段的，相当于迭代器
  protocol::HttpHeaderCursor reqCursor(req);
  std::string name;
  std::string value;
  // 迭代器的成员函数，用于遍历下一行
  while (reqCursor.next(name, value)) {
    cerr << "name = " << name << " value = " << value << "\n";
  }

  // 打印响应行的版本，状态码，原因
  cerr << resp->get_http_version() << " " << resp->get_status_code() << " "
       << resp->get_reason_phrase() << "\n";
  protocol::HttpHeaderCursor respCursor(resp);
  while (respCursor.next(name, value)) {
    cerr << "name = " << name << " value = " << value << "\n";
  }

  // 读取响应体
  // const void *说明可以更改指向，但不能更改指向的值
  // 并且http协议传输的不一定是文本信息
  const void *body;
  size_t body_len;
  // 传入一个二级指针是为了更改指针的指向
  resp->get_parsed_body(&body, &body_len);
  // 强转为字符串信息
  cerr << static_cast<const char *>(body) << "\n";
}
int main() {
  signal(SIGINT, sighandler);  // 注册2号信号，可以执行有条件的等待

  // 一个HTTP的任务，用于记录请求信息和响应信息
  // 调用任务工厂中的工厂函数创建一个任务，说明不希望用户决定对象的创建和销毁
  // 工厂函数需要传入URL，最大重定向次数，最大重试次数和回调函数
  WFHttpTask *httpTask = WFTaskFactory::create_http_task("http://www.baidu.com",
                                                         10, 10, httpCallback);

  // 任务的开始，实际上是将任务交给框架
  // 由框架调用资源运行任务
  httpTask->start();
  // 让主线程在执行任务前阻塞
  waitGroup.wait();
  cout << "finished!\n";
  return 0;
}
