#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <cassert>
#include <atomic>
#include <pthread.h>
#include "Timer.h"

extern "C" {
#include "ppmb_io.h"
}

struct img {
  int xsize;
  int ysize;
  int maxrgb;
  unsigned char *r;
  unsigned char *g;
  unsigned char *b;
};

void print_histogram(FILE *f, int *hist, int N) {
  fprintf(f, "%d\n", N+1);
  for(int i = 0; i <= N; i++) {
    fprintf(f, "%d %d\n", i, hist[i]);
  }
}

// 优化的顺序锁 - 使用原子操作，避免嵌套锁
class alignas(64) SequencialLock {  // 优化2：缓存行对齐
  std::atomic<int> head;
  std::atomic<int> tail;
  char padding[56];  // 填充到 64 字节，避免 false sharing
  
public:
  SequencialLock() : head(0), tail(0) {}
  
  void lock() {
    // 优化1：直接使用原子操作获取票号，无需额外的锁
    int current_num = tail.fetch_add(1, std::memory_order_relaxed);
    
    // 自旋等待轮到自己
    while (head.load(std::memory_order_acquire) != current_num) {
      // 优化：添加 CPU pause 指令，降低功耗
      #if defined(__x86_64__) || defined(__i386__)
      __builtin_ia32_pause();
      #elif defined(__aarch64__)
      __asm__ __volatile__("yield");
      #endif
    }
  }
  
  void unlock() {
    // 优化1：直接原子递增，无需额外的锁
    head.fetch_add(1, std::memory_order_release);
  }
};

// 优化2：缓存行对齐的锁数组
alignas(64) SequencialLock r_lock[256];
alignas(64) SequencialLock g_lock[256];
alignas(64) SequencialLock b_lock[256];

struct index {
  struct img *input;
  int *hist_r;
  int *hist_g;
  int *hist_b;
  int start;
  int end;
};

void* lock_histogram(void *thread) {
  struct index *idx = (struct index *) thread;
  struct img *input = idx->input;
  int *hist_r = idx->hist_r;
  int *hist_g = idx->hist_g;
  int *hist_b = idx->hist_b;
  int start = idx->start;
  int end = idx->end;

  // 优化3：批量处理 - 使用局部直方图减少锁操作
  int local_hist_r[256] = {0};
  int local_hist_g[256] = {0};
  int local_hist_b[256] = {0};

  // 第一阶段：在本地直方图中累积（无锁操作）
  for(int pix = start; pix < end; pix++) {
    local_hist_r[input->r[pix]]++;
    local_hist_g[input->g[pix]]++;
    local_hist_b[input->b[pix]]++;
  }

  // 第二阶段：批量更新全局直方图
  // 处理红色通道
  for(int i = 0; i < 256; i++) {
    if(local_hist_r[i] > 0) {
      r_lock[i].lock();
      hist_r[i] += local_hist_r[i];
      r_lock[i].unlock();
    }
  }

  // 处理绿色通道
  for(int i = 0; i < 256; i++) {
    if(local_hist_g[i] > 0) {
      g_lock[i].lock();
      hist_g[i] += local_hist_g[i];
      g_lock[i]. unlock();
    }
  }

  // 处理蓝色通道
  for(int i = 0; i < 256; i++) {
    if(local_hist_b[i] > 0) {
      b_lock[i].lock();
      hist_b[i] += local_hist_b[i];
      b_lock[i].unlock();
    }
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if(argc != 4) {
    printf("Usage: %s input-file output-file threads\n", argv[0]);
    exit(1);
  }
  
  char *output_file = argv[2];
  char *input_file = argv[1];
  int threads = atoi(argv[3]);

  struct img input;

  if(!ppmb_read(input_file, &input.xsize, &input.ysize, &input.maxrgb, 
		&input.r, &input. g, &input.b)) {
    if(input.maxrgb > 255) {
      printf("Maxrgb %d not supported\n", input.maxrgb);
      exit(1);
    }

    int *hist_r, *hist_g, *hist_b;

    hist_r = (int *) calloc(input.maxrgb+1, sizeof(int));
    hist_g = (int *) calloc(input.maxrgb+1, sizeof(int));
    hist_b = (int *) calloc(input.maxrgb+1, sizeof(int));

    ggc::Timer t("histogram");

    t.start();
    pthread_t thread_ids[threads];
    struct index *idx = (struct index *) malloc(sizeof(struct index) * threads);
    int N = input.xsize * input.ysize;

    for (int i = 0; i < threads; i++) {
      idx[i].input = &input;
      idx[i].hist_r = hist_r;
      idx[i].hist_g = hist_g;
      idx[i].hist_b = hist_b;
      idx[i]. start = N*i/threads;
      idx[i].end = N*(i+1)/threads;
      pthread_create(&thread_ids[i], NULL, lock_histogram, (void *) (idx+i));
    }

    for (int i = 0; i < threads; i++) {
      pthread_join(thread_ids[i], NULL);
    }

    t.stop();

    FILE *out = fopen(output_file, "w");
    if(out) {
      print_histogram(out, hist_r, input.maxrgb);
      print_histogram(out, hist_g, input.maxrgb);
      print_histogram(out, hist_b, input.maxrgb);
      fclose(out);
    } else {
      fprintf(stderr, "Unable to output!\n");
    }
    printf("Time: %llu ns\n", t. duration());
    
    free(hist_r);
    free(hist_g);
    free(hist_b);
    free(idx);
  }
  
  return 0;
}