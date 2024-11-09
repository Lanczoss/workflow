// WaitGroup 让某个线程处于等待状态
#include <workflow/WFFacilities.h>
// WFHttpServer.h 包含server类的设计
#include <signal.h>
#include <unistd.h>
#include <workflow/WFHttpServer.h>

#include <iostream>
#include <string>
using std::cout;
using std::string;
struct SeriesContext {
  string name;
  string password;
  protocol::HttpResponse *resp;  // 决定了回复给客户端的内容
};
static WFFacilities::WaitGroup waitGroup(1);

void sigHandler(int signum) {
  cout << "done!\n";
  waitGroup.done();
}

void redisCallback(WFRedisTask *redisTask) {
  // 获取redis的执行结果
  protocol::RedisValue result;
  protocol::RedisResponse *resp = redisTask->get_resp();
  resp->get_result(result);

  // 访问context
  SeriesContext *context =
      static_cast<SeriesContext *>(series_of(redisTask)->get_context());
  if (result.is_string() && result.string_value() == context->password) {
    cout << context->name << " success\n";
    context->resp->append_output_body("<html>success</html>");
  } else {
    cout << context->name << " fail\n";
    context->resp->append_output_body("<html>fail</html>");
  }
}
void process(WFHttpTask *serverTask) {  // 回复给客户端的响应
                                        // 一定是serverTask的resp决定的
  protocol::HttpRequest *req = serverTask->get_req();
  string uri = req->get_request_uri();
  cout << uri << "\n";
  // /login?username=niqiu&password=123
  string nameKV = uri.substr(0, uri.find("&"));
  string passwordKV = uri.substr(uri.find("&") + 1);
  string name = nameKV.substr(nameKV.find("=") + 1);
  string password = passwordKV.substr(passwordKV.find("=") + 1);
  cout << "name = " << name << ", password = " << password << "\n";
  // 申请一片内存空间，用于序列内的任务共享数据
  SeriesContext *context = new SeriesContext;
  context->name = name;
  context->password = password;
  context->resp = serverTask->get_resp();
  series_of(serverTask)->set_context(context);
  // 创建一个redis任务
  // 用于验证账户密码是否有效
  WFRedisTask *redisTask = WFTaskFactory::create_redis_task(
      "redis://127.0.0.1:6379", 10, redisCallback);
  // hget 57user niqiu
  redisTask->get_req()->set_request("HGET", {"57user", name});
  series_of(serverTask)->push_back(redisTask);
}
int main() {
  // 注册一个信号
  signal(SIGINT, sigHandler);
  // 开启一个http对话，传入一个回调函数
  // 回调函数参数是WFHttpTask*
  WFHttpServer server(process);
  // 传入http对话端口
  if (server.start(12345) == 0) {
    waitGroup.wait();
    server.stop();
  } else {
    perror("server start failed!");
    return -1;
  }
  cout << "finished!\n";
  return 0;
}
