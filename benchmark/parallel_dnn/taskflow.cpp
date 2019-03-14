#include "dnn.hpp"
#include <taskflow/taskflow.hpp>  

class DNNTrainingPattern : public tf::Framework {

  public:

    DNNTrainingPattern() { 
      init_dnn(_dnn); 
      _build_task_graph();
    };

    void validate(Eigen::MatrixXf &mat, Eigen::VectorXi &vec) {
      _dnn.validate(mat, vec);
    }

  private:

    MNIST_DNN _dnn;

    void _build_task_graph() {
      auto f_task = emplace([&]() { forward_task(_dnn, IMAGES, LABELS); });

      std::vector<tf::Task> backward_tasks;
      std::vector<tf::Task> update_tasks;

      for(int j=_dnn.acts.size()-1; j>=0; j--) {
        // backward propagation
        auto& b_task = backward_tasks.emplace_back(emplace(
          [&, i=j] () { backward_task(_dnn, i, IMAGES); }
        ));

        // update weight 
        auto& u_task = update_tasks.emplace_back(
          emplace([&, i=j] () { _dnn.update(i); })
        );

        if(j + 1u == _dnn.acts.size()) {
          f_task.precede(b_task);
        }
        else {
          backward_tasks[backward_tasks.size()-2].precede(b_task);
        }
        b_task.precede(u_task);
      }  
    }
};

class DNN : public tf::Framework {
  public:

    DNN(DNNTrainingPattern &dnn_pattern) : _dnn_pattern(dnn_pattern) {
      std::vector<tf::Task> tasks;
      for(auto i=0u; i<NUM_ITERATIONS; i++) {
        tasks.emplace_back(composed_of(_dnn_pattern));
      }
      linearize(tasks);
    }

  private:

    DNNTrainingPattern& _dnn_pattern;
};

void run_taskflow(unsigned num_epochs, unsigned num_threads) {

  tf::Taskflow tf {num_threads};

  auto dnn_patterns = std::make_unique<DNNTrainingPattern[]>(NUM_DNNS);
  auto dnns = std::make_unique<DNN*[]>(NUM_DNNS); 

  for(size_t i=0; i<NUM_DNNS; i++) {
    dnns[i] = new DNN (dnn_patterns[i]);
  }

  std::vector<tf::Task> tasks;
  tf::Framework parallel_dnn;
  for(size_t i=0; i<NUM_DNNS; i++) {
    parallel_dnn.composed_of(*(dnns[i]));
  }

  auto t1 = std::chrono::high_resolution_clock::now();
  parallel_dnn.emplace([&](){
    for(size_t i=0; i<NUM_DNNS; i++) {
      std::cout << "Validate " << i << "th NN: ";
      dnn_patterns[i].validate(TEST_IMAGES, TEST_LABELS);
    }
    shuffle(IMAGES, LABELS);
    report_runtime(t1);
  }).gather(tasks);
  
  std::cout << parallel_dnn.dump() << std::endl;

  //tf.run_n(parallel_dnn, 100).get();

}

