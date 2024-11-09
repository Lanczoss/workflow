#include <signal.h>
#include <workflow/MySQLResult.h>
#include <workflow/MySQLUtil.h>
#include <workflow/WFFacilities.h>

#include <iostream>
using std::cerr;
using std::string;
static WFFacilities::WaitGroup waitGroup(1);

void sighandler(int signum) {
  std::cout << "signum = " << signum << "\n";
  waitGroup.done();
}

void mysqlCallback(WFMySQLTask *mysqlTask) {
  if (mysqlTask->get_state() != WFT_STATE_SUCCESS) {
    // 在系统层面报错，权限or密码
    cerr << "error_msg =  "
         << WFGlobal::get_error_string(mysqlTask->get_state(),
                                       mysqlTask->get_error())
         << "\n";
    return;
  }
  protocol::MySQLResponse *resp = mysqlTask->get_resp();
  if (resp->get_packet_type() == MYSQL_PACKET_ERROR) {
    // 在SQL语句报错
    cerr << "error_code = " << resp->get_error_code()
         << " error_msg = " << resp->get_error_msg() << "\n";
    return;
  }
  // 用于遍历获取结果，类似迭代器
  protocol::MySQLResultCursor cursor(resp);
  do {
    if (cursor.get_cursor_status() == MYSQL_STATUS_OK) {
      // 写类型的SQL语句
      cerr << "write \n";
      cerr << cursor.get_affected_rows() << " rows affected\n";
    } else if (cursor.get_cursor_status() == MYSQL_STATUS_GET_RESULT) {
      // 读类型的SQL语句
      cerr << "read \n";
      // 读表头 列的信息 field
      const protocol::MySQLField *const *fieldArr;
      fieldArr = cursor.fetch_fields();
      for (int i = 0; i < cursor.get_field_count(); ++i) {
        cerr << "db = " << fieldArr[i]->get_db()
             << " table = " << fieldArr[i]->get_table()
             << " name = " << fieldArr[i]->get_name()
             << " type = " << datatype2str(fieldArr[i]->get_data_type())
             << "\n";
      }
      // 读表的内容 每一行每一列
      // bool fetch_all(std::vector<std::vector<MySQLCell>>& rows);
      std::vector<std::vector<protocol::MySQLCell>> rows;
      cursor.fetch_all(rows);
      for (auto &row : rows) {
        for (auto &cell : row) {
          if (cell.is_int()) {
            cerr << cell.as_int();
          } else if (cell.is_string()) {
            cerr << cell.as_string();
          } else if (cell.is_datetime()) {
            cerr << cell.as_datetime();
          }
          cerr << "\t";
        }
        cerr << "\n";
      }
    }
  } while (cursor.next_result_set());  // mysql 任务支持一个任务处理多个SQL语句
}
int main() {
  // 注册2号信号
  signal(SIGINT, sighandler);
  // 创建一个MySQL的任务
  // 使用工厂函数
  WFMySQLTask *mysqlTask = WFTaskFactory::create_mysql_task(
      "mysql://root:123@localhost", 1, mysqlCallback);
  // 准备需要操作的SQL语句
  string sql =
      "insert into mycloud.tbl_user_token (user_name,user_token) values "
      "('Caixukun','singdancerap');";
  // string sql;
  // 组合sql语句，以;分隔
  sql += "select * from mycloud.tbl_user_token;";
  // 获取请求语句并设置sql语句
  mysqlTask->get_req()->set_query(sql);
  mysqlTask->start();
  waitGroup.wait();
  return 0;
}
