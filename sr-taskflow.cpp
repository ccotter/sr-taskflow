#include <exec/any_sender_of.hpp>
#include <exec/async_scope.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/task.hpp>
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <format>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

template <class... Ts>
using any_sender_of = typename exec::any_receiver_ref<stdexec::completion_signatures<Ts...>>::template any_sender<>;

struct TaskFlow {
  // A TaskFlow inspired dynamic graph construction implementation
  // using Sender/Receiver.

  struct Task {
    any_sender_of<stdexec::set_stopped_t(), stdexec::set_error_t(std::exception_ptr), stdexec::set_value_t()> sender;

    template <class Sender>
    Task(Sender&& s) : sender(std::forward<Sender>(s)) {}

    void precede(Task& other) {
      successors_.push_back(&other);
    }

    std::vector<Task*> successors_;
    std::atomic<int> num_predecessors{0};
  };

  template <class Sender>
  Task& emplace(Sender&& sender) {
    tasks_.emplace_back(std::make_shared<Task>(std::forward<Sender>(sender)));
    return *tasks_.back();
  }

  auto run() {
    for (std::shared_ptr<Task>& task: tasks_) {
      ++task->num_predecessors;
      for (Task* successor: task->successors_) {
        ++successor->num_predecessors;
      }
    }

    auto process_successors = [](auto& scope, auto spawn_one, auto process_successors, Task* task) -> void {
      for (Task* successor: task->successors_) {
        if (successor->num_predecessors.fetch_sub(1) == 1) {
          spawn_one(scope, spawn_one, process_successors, successor);
        }
      }
    };
    auto spawn_one = [](auto& scope, auto spawn_one, auto process_successors, Task* task) -> void {
      scope.spawn(std::move(task->sender) | stdexec::then([&scope, spawn_one, process_successors, task] {
        process_successors(scope, spawn_one, process_successors, task);
      }));
    };

    for (std::shared_ptr<Task>& task: tasks_) {
      if (task->num_predecessors.fetch_sub(1) == 1) {
        spawn_one(scope_, spawn_one, process_successors, task.get());
      }
    }
    return scope_.on_empty();
  }

  std::vector<std::shared_ptr<Task>> tasks_;
  exec::async_scope scope_;
};

struct PrintBuffer {
  // Thread synchronized std::cout

  PrintBuffer() : buffer_(std::make_shared<std::stringstream>()) {}
  ~PrintBuffer() {
    if (buffer_.use_count() == 1) {
      std::lock_guard lg(mutex_);
      std::cout << buffer_->str();
    }
  }
  template <class T>
  PrintBuffer operator<<(T&& t) {
    *buffer_ << std::forward<T>(t);
    return *this;
  }
  std::shared_ptr<std::stringstream> buffer_;
  static std::mutex mutex_;
};
std::mutex PrintBuffer::mutex_;

decltype(auto) print() {
  static auto start = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start).count();
  return PrintBuffer{} << "[time=" << std::setfill(' ') << std::setw(5) << ms << "ms][tid=" << std::this_thread::get_id() << "] ";
}

int main() {
  exec::static_thread_pool pool{4};
  auto pool_scheduler = pool.get_scheduler();

  // XXX Change the demo to use a timer capable schedule to allow
  // 'co_await sleep_for()' or something more interesting such
  // as network based senders.
  //
  // this_thread::sleep_for is a hacky substitute to demonstrate compute bound work
  // using a thread pool.

  auto make_task = [&](std::string name) {
    return stdexec::on(
      pool_scheduler,
      stdexec::just() | stdexec::then([name]() noexcept { std::this_thread::sleep_for(100ms); print() << name << "\n"; })
    );
  };
  auto make_coro_task = [&](std::string name) {
    return stdexec::on(
      pool_scheduler,
      [](std::string name) -> exec::task<void> {
        std::this_thread::sleep_for(100ms);
        print() << name << "\n";
        co_return;
      }(std::move(name))
    );
  };

  TaskFlow flow;
  auto& A = flow.emplace(make_task("A"));
  auto& B = flow.emplace(make_task("B"));
  auto& C = flow.emplace(make_coro_task("C"));
  auto& D = flow.emplace(make_task("D"));
  A.precede(B);
  A.precede(C);
  B.precede(D);
  C.precede(D);
  stdexec::sync_wait(flow.run());
  return 0;
}
