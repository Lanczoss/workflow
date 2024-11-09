// http客户端
#include <signal.h>
#include <workflow/HttpUtil.h>
#include <workflow/WFFacilities.h>
#include <workflow/WFRedisServer.h>
#include <workflow/WFTaskFactory.h>
#include <workflow/Workflow.h>

#include <iostream>
using std::cerr;
using std::cout;

struct SeriesContext {
  void *resp;
  size_t resp_len;
};

static WFFacilities::WaitGroup waitGroup(1);  // 让主线程阻塞用的对象
void sighandler(int signum) {                 // 信号处理函数
  waitGroup.done();  // 遇到2号信号时，执行一次done()可以主线程结束
  cout << "done\n";
}

void redisCallback(WFRedisTask *redisTask) {
  protocol::RedisRequest *req = redisTask->get_req();
  int state = redisTask->get_state();
  int error = redisTask->get_error();
  // val用来保存redis执行的结果
  protocol::RedisValue val;
  switch (state) {
    case WFT_STATE_SYS_ERROR:
      cerr << "system error: " << strerror(error) << "\n";
      break;
    case WFT_STATE_DNS_ERROR:
      cerr << "DNS error: " << gai_strerror(error) << "\n";
      break;
    case WFT_STATE_SSL_ERROR:
      cerr << "SSL error\n";
      break;
    case WFT_STATE_TASK_ERROR:
      cerr << "Task error\n";
      break;
    case WFT_STATE_SUCCESS:
      if (val.is_error()) {
        cerr << "Error reply. Need a password?\n";
        state = WFT_STATE_TASK_ERROR;
      }
      break;
  }
  if (state != WFT_STATE_SUCCESS) {
    cerr << "Failed. Press Ctrl-C to exit.\n";
    return;
  } else {
    cerr << "Save success\n";
  }
}

// http任务的回调函数，传入的是http任务参数
void httpCallback(WFHttpTask *task) {
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

  // 读取响应体
  // const void *说明可以更改指向，但不能更改指向的值
  // 并且http协议传输的不一定是文本信息
  SeriesContext *context =
      static_cast<SeriesContext *>(series_of(task)->get_context());
  const void *body = context->resp;
  // 传入一个二级指针是为了更改指针的指向
  resp->get_parsed_body(&body, &context->resp_len);
  // cerr << (char *)body << "\n";
  WFRedisTask *redis = WFTaskFactory::create_redis_task(
      "redis://127.0.0.1:6379", 10, redisCallback);
  protocol::RedisRequest *req = redis->get_req();
  req->set_request("set", {"www.taobao.com", std::string((char *)body)});
  series_of(task)->push_back(redis);
}

int main() {
  signal(SIGINT, sighandler);  // 注册2号信号，可以执行有条件的等待

  // 一个HTTP的任务，用于记录请求信息和响应信息
  // 调用任务工厂中的工厂函数创建一个任务，说明不希望用户决定对象的创建和销毁
  // 工厂函数需要传入URL，最大重定向次数，最大重试次数和回调函数
  WFHttpTask *httpTask = WFTaskFactory::create_http_task(
      "http://www.taobao.com", 10, 10, httpCallback);

  SeriesWork *series = Workflow::create_series_work(httpTask, nullptr);
  // 申请context的内存空间
  SeriesContext *context = new SeriesContext();
  series->set_context(context);

  // 任务的开始，实际上是将任务交给框架
  // 由框架调用资源运行任务
  series->start();
  // 让主线程在执行任务前阻塞
  waitGroup.wait();
  cout << "finished!\n";
  return 0;
}
