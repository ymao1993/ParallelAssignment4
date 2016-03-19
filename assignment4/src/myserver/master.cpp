#include <glog/logging.h>
#include <stdio.h>
#include <stdlib.h>

#include "server/messages.h"
#include "server/master.h"
#include <queue>
#include <map>

#define MAX_EXEC_CONTEXT 24
#define INIT_NUM_WORKER 2
#define THRESHOLD 5

/**
 * Enum type of work
 */
 enum Work_type {
  WISDOM,
  PROJECTIDEA,
  TELLMENOW,
  COUNTPRIMES,
  COMPAREPRIMES,
  NUM_OF_WORKTYPE
 };

/**
 * Request_info records the request's info
 */
typedef struct _Request_info {
  Client_handle client_handle;
  Request_msg client_req;

  _Request_info(Client_handle client_handle, Request_msg client_req):
  client_handle(client_handle),
  client_req(client_req){}

} Request_info;

/**
 * the state of worker
 */
typedef struct _Worker_state {
  Worker_handle worker_handle;
  int num_running_task;
  int num_work_type[5];
} Worker_state;

/**
 * the state of the master
 */
static struct Master_state {

  //tag counter
  int next_tag;

  //is the server ready to go?
  bool server_ready;
  
  //max number of workers configured
  int max_num_workers;

  //current number of workers
  int current_num_wokers;

  //the number of all pending requests:
  //pending requests =request being processed + requests in the queue
  int num_pending_client_requests;

  //workers
  std::vector<Worker_state> my_workers;

  //request queue
  std::queue<Request_info> requests_queue;

  //vip request requests_queue
  std::queue<Request_info> requests_queue_vip;

  //tag client map
  std::map<int, Client_handle> tagClientMap;

  //tag type map
  std::map<int, int> tagTypeMap;

} mstate;



void master_node_init(int max_workers, int& tick_period) {

  // set up tick handler to fire every 5 seconds. (feel free to
  // configure as you please)
  tick_period = 5;

  mstate.next_tag = 0;
  mstate.max_num_workers = max_workers;
  mstate.num_pending_client_requests = 0;
  mstate.current_num_wokers = 0;

  // don't mark the server as ready until the server is ready to go.
  // This is actually when the first worker is up and running, not
  // when 'master_node_init' returnes
  mstate.server_ready = false;

  // fire up new workers
  int initNumWorker = max_workers;
  for(int i = 0; i<initNumWorker; i++)
  {
    int tag = random();
    Request_msg req(tag);
    req.set_arg("name", "my worker " + i);
    request_new_worker_node(req);
  }

}

void handle_new_worker_online(Worker_handle worker_handle, int tag) {

  // 'tag' allows you to identify which worker request this response
  // corresponds to.  Since the starter code only sends off one new
  // worker request, we don't use it here.
  Worker_state state;
  state.worker_handle = worker_handle;
  state.num_running_task = 0;
  mstate.current_num_wokers++;

  // init num_work_type
  for (int i = 0; i < NUM_OF_WORKTYPE; i ++)
  {
    state.num_work_type[i] = 0;
  }
  
  mstate.my_workers.push_back(state);

  // Now that a worker is booted, let the system know the server is
  // ready to begin handling client requests.  The test harness will
  // now start its timers and start hitting your server with requests.
  if (mstate.server_ready == false) {
    server_init_complete();
    mstate.server_ready = true;
  }
}

void handle_worker_response(Worker_handle worker_handle, const Response_msg& resp) {

  // Master node has received a response from one of its workers.
  // Here we directly return this response to the client.

  DLOG(INFO) << ">>>Master received a response from a worker: [" << resp.get_tag() << ":" << resp.get_response() << "]" << std::endl;

  Client_handle client = mstate.tagClientMap[resp.get_tag()];
  send_client_response(client, resp);
  DLOG(INFO) << "<<Master send response back to client: " << client << std::endl;

  mstate.num_pending_client_requests --;
  DLOG(INFO) << "pending_client_requests: " <<  mstate.num_pending_client_requests << std::endl;

  //find the worker
  for(unsigned int i=0; i< mstate.my_workers.size(); i++)
  {
    if(mstate.my_workers[i].worker_handle == worker_handle)
    {
      // update mstate: num_work_type, num_running_task
      int tag = resp.get_tag();
      int type = mstate.tagTypeMap[tag];
      mstate.my_workers[i].num_work_type[type]--;
      mstate.my_workers[i].num_running_task--;
      break;
    }
  }
  // re-dispatching vip queue 
  for (unsigned int i = 0; i < mstate.requests_queue_vip.size(); i++)
  {
    Request_info req = mstate.requests_queue_vip.front();
    DLOG(INFO) << "deque(vip):" << req.client_req.get_request_string()<< std::endl;
    mstate.requests_queue_vip.pop();
    handle_client_request(req.client_handle, req.client_req);
  }
  // re-dispatching queue
  for (unsigned int i = 0; i < mstate.requests_queue.size(); i++)
  {
    Request_info req = mstate.requests_queue.front(); 
    DLOG(INFO) << "deque:" << req.client_req.get_request_string()<< std::endl;
    mstate.requests_queue.pop();
    handle_client_request(req.client_handle, req.client_req);
  }

  return;

}

void handle_client_request(Client_handle client_handle, const Request_msg& client_req) {

  DLOG(INFO) << ">>Received request: " << client_req.get_request_string() << std::endl;
  std::string request_arg = client_req.get_arg("cmd");

  // You can assume that traces end with this special message.  It
  // exists because it might be useful for debugging to dump
  // information about the entire run here: statistics, etc.
  if (request_arg == "lastrequest") {
    Response_msg resp(0);
    resp.set_response("ack");
    send_client_response(client_handle, resp);
    return;
  }

  mstate.num_pending_client_requests++;
  bool is_assigned = false;

  // Assign to worker base on its work_status
  // assign to low workload node 
  if (request_arg == "418wisdom")
  {
    int min = mstate.my_workers[0].num_work_type[WISDOM];
    int min_index = 0;
    for(unsigned int i=0; i<mstate.my_workers.size(); i++)
    {
      if (min > mstate.my_workers[i].num_work_type[WISDOM] && 
          !mstate.my_workers[i].num_work_type[PROJECTIDEA])
      {
        min = mstate.my_workers[i].num_work_type[WISDOM];
        min_index = i;
      }

    }

    if (min <= MAX_EXEC_CONTEXT / 2)
    {
      Worker_handle worker_handle = mstate.my_workers[min_index].worker_handle;
      int tag = mstate.next_tag++;
      Request_msg worker_req(tag, client_req);
      mstate.tagClientMap[tag] = client_handle;
      mstate.tagTypeMap[tag] = WISDOM;
      
      send_request_to_worker(worker_handle, worker_req);
      mstate.my_workers[min_index].num_work_type[WISDOM]++;
      mstate.my_workers[min_index].num_running_task++;
      is_assigned = true;
    }
  }
  // assign to idle node 
  if (request_arg == "projectidea")
  {
    for(unsigned int i=0; i<mstate.my_workers.size(); i++)
    {
      if(mstate.my_workers[i].num_running_task <= 1)
      {
        Worker_handle worker_handle = mstate.my_workers[i].worker_handle;
        int tag = mstate.next_tag++;
        Request_msg worker_req(tag, client_req);
        mstate.tagClientMap[tag] = client_handle;
        mstate.tagTypeMap[tag] = PROJECTIDEA;
        
        send_request_to_worker(worker_handle, worker_req);
        mstate.my_workers[i].num_work_type[PROJECTIDEA]++;
        mstate.my_workers[i].num_running_task++;
        is_assigned = true;
        break;
      } 
    }
  }
  // find any possible spot
  if (request_arg == "tellmenow")
  {
    for(unsigned int i=0; i<mstate.my_workers.size(); i++)
    {
      if(mstate.my_workers[i].num_running_task < MAX_EXEC_CONTEXT)
      {
        Worker_handle worker_handle = mstate.my_workers[i].worker_handle;
        int tag = mstate.next_tag++;
        Request_msg worker_req(tag, client_req);
        mstate.tagClientMap[tag] = client_handle;
        mstate.tagTypeMap[tag] = TELLMENOW;
        
        send_request_to_worker(worker_handle, worker_req);
        mstate.my_workers[i].num_work_type[TELLMENOW]++;
        mstate.my_workers[i].num_running_task++;
        is_assigned = true;
        break;
      }
    }
  }
  // find node that has low workload
  if (request_arg == "countprimes")
  {
    for(unsigned int i=0; i<mstate.my_workers.size(); i++)
    {

      if(!mstate.my_workers[i].num_work_type[WISDOM] && 
         !mstate.my_workers[i].num_work_type[PROJECTIDEA] &&
         mstate.my_workers[i].num_running_task < MAX_EXEC_CONTEXT)
      {
        Worker_handle worker_handle = mstate.my_workers[i].worker_handle;
        int tag = mstate.next_tag++;
        Request_msg worker_req(tag, client_req);
        mstate.tagClientMap[tag] = client_handle;
        mstate.tagTypeMap[tag] = COUNTPRIMES;
        
        send_request_to_worker(worker_handle, worker_req);
        mstate.my_workers[i].num_work_type[COUNTPRIMES]++;
        mstate.my_workers[i].num_running_task++;
        is_assigned = true;
        break;
      }
    }
  }
  // find node that has more than 4 context
  if (request_arg == "compareprimes")
  {
    for(unsigned int i=0; i<mstate.my_workers.size(); i++)
    {
      if(!mstate.my_workers[i].num_work_type[WISDOM] && 
         !mstate.my_workers[i].num_work_type[PROJECTIDEA] &&
         mstate.my_workers[i].num_running_task < MAX_EXEC_CONTEXT)
      {
        Worker_handle worker_handle = mstate.my_workers[i].worker_handle;
        int tag = mstate.next_tag++;
        Request_msg worker_req(tag, client_req);
        mstate.tagClientMap[tag] = client_handle;
        mstate.tagTypeMap[tag] = COMPAREPRIMES;
        
        send_request_to_worker(worker_handle, worker_req);
        mstate.my_workers[i].num_work_type[COMPAREPRIMES]++;
        mstate.my_workers[i].num_running_task++;
        is_assigned = true;
        break;
      }
    }
  }


  //if all workers are busy, push the request to queue
  if(!is_assigned)
  {
    if (request_arg == "tellmenow")
    {
      DLOG(INFO) << "enque vip:" << client_req.get_tag() << ":" << client_req.get_request_string() << std::endl;
      mstate.requests_queue_vip.push(Request_info(client_handle, client_req));
    }
    else
    {
      DLOG(INFO) << "enque:" << client_req.get_tag() << ":" << client_req.get_request_string() << std::endl;
      mstate.requests_queue.push(Request_info(client_handle, client_req));
    }  
  }

  // We're done!  This event handler now returns, and the master
  // process calls another one of your handlers when action is
  // required.
  return;

}

void handle_tick() {

  // TODO: you may wish to take action here.  This method is called at
  // fixed time intervals, according to how you set 'tick_period' in
  // 'master_node_init'.
  // DLOG(INFO) << "request_queue size: " << mstate.requests_queue.size() << std::endl;
  // DLOG(INFO) << "request_queue vip size: " << mstate.requests_queue_vip.size() << std::endl;
  // if (!mstate.requests_queue.size() && mstate.my_workers.size() > 1)
  // {
  //   for (unsigned int i = 0; i < mstate.my_workers.size(); i++)
  //   {
  //     if (!mstate.my_workers[i].num_running_task)
  //     {
  //       kill_worker_node(mstate.my_workers[i].worker_handle);
  //       mstate.my_workers.erase(mstate.my_workers.begin()+i);
  //       mstate.current_num_wokers--;
  //     }
  //   }
  // }
  // else if (mstate.requests_queue.size() > THRESHOLD && 
  //          mstate.current_num_wokers < master.max_num_workers)
  // {
  //   int workerid = random();
  //   Request_msg req(workerid);
  //   req.set_arg("name", "my worker " + workerid);
  //   request_new_worker_node(req);
  // }
}

