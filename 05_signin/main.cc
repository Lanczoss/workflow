#include <fcntl.h>
#include <signal.h>
#include <workflow/WFFacilities.h>
#include <workflow/WFHttpServer.h>
#include <workflow/WFTaskFactory.h>

// #include <fstream>
#include <iostream>
#include <memory>
#include <string>

using std::cerr;
using std::cout;
using std::ifstream;
using std::string;
using std::unique_ptr;

WFFacilities::WaitGroup waitGrop(1);
void signalhander(int num) {
  cout << "Server closed!\n";
  waitGrop.done();
}

struct ServerContext {
  string password;
  protocol::HttpResponse *resp;
};

// redis的回调函数
void redisCallback(WFRedisTask *redisTask) {
  protocol::RedisResponse *resp = redisTask->get_resp();
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
      resp->get_result(val);
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
    ServerContext *context =
        static_cast<ServerContext *>(series_of(redisTask)->get_context());
    if (val.is_string() && val.string_value() == context->password) {
      context->resp->append_output_body("<html>Login Success!</html>");
    } else {
      context->resp->append_output_body("<html>Login Failed!</html>");
    }
  }
}

// WFHttpServer的base job
void process(WFHttpTask *serverTask) {
  // cout << "1\n";
  // 如果是GET先返回一个sigin.html
  protocol::HttpRequest *req = serverTask->get_req();
  protocol::HttpResponse *resp = serverTask->get_resp();
  // 需要创建一个context共享响应体
  ServerContext *context = new ServerContext();
  context->resp = resp;

  series_of(serverTask)->set_context(context);
  // cout << req->get_method() << "\n";
  string method = req->get_method();
  string uri = req->get_request_uri();
  if (method == "GET") {
    if (uri == "/login") {
      // 返回postform.html
      unique_ptr<char[]> buf(new char[615]());
      // ifstream ifs("postform.html");
      // ifs.read(buf.get(), 614);
      WFFileIOTask *fileTask = WFTaskFactory::create_pread_task(
          "postform.html", buf.get(), 614, 0, [resp](WFFileIOTask *preadTask) {
            resp->append_output_body(preadTask->get_args()->buf, 615);
          });
      // resp->append_output_body(buf.get());
      series_of(serverTask)->push_back(fileTask);
    }
  } else if (method == "POST") {
    // 用户发送用户名和密码
    const void *body;
    size_t length;
    req->get_parsed_body(&body, &length);
    // username=liao&password=11
    // cout << static_cast<const char *>(body) << "\n";
    string str_body = static_cast<const char *>(body);
    string usernameKV = str_body.substr(0, str_body.find("&"));
    string username = usernameKV.substr(usernameKV.find("=") + 1);
    string passwordKV = str_body.substr(str_body.find("&") + 1);
    string password = passwordKV.substr(str_body.find("=") + 1);

    // 创建一个redis任务用于验证密码是否正确
    WFRedisTask *redisTask = WFTaskFactory::create_redis_task(
        "redis://127.0.0.1:6379", 10, redisCallback);
    protocol::RedisRequest *req = redisTask->get_req();
    req->set_request("hget", {"userinfo", username});

    // cout << password << "\n";
    context->password = password;

    series_of(serverTask)->push_back(redisTask);
  }

  serverTask->set_callback([context](WFHttpTask *) { delete context; });
}

int main(void) {
  // 注册2号信号
  signal(SIGINT, signalhander);

  // 创建一个WFHttpServer
  WFHttpServer server(process);
  if (server.start(12345) == 0) {
    // 这里说明服务端启动成功
    waitGrop.wait();
    server.stop();
  } else {
    perror("http server start");
    return -1;
  }
  return 0;
}
