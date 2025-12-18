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

// 优化1: 缓存行对齐的原子类型
struct alignas(64) AlignedAtomic {
  std::atomic<int> value;
  char padding[60];  // 填充到 64 字节
  
  AlignedAtomic() : value(0) {}
};

struct index {
  struct img *input;
  AlignedAtomic *hist_r;
  AlignedAtomic *hist_g;
  AlignedAtomic *hist_b;
  int start;
  int end;
};

void* lockfree_histogram(void * thread) {
  struct index *idx = (struct index *) thread;
  struct img *input = idx->input;
  AlignedAtomic *hist_r = idx->hist_r;
  AlignedAtomic *hist_g = idx->hist_g;
  AlignedAtomic *hist_b = idx->hist_b;
  int start = idx->start;
  int end = idx->end;

  // 优化2: 批量处理 - 本地累积
  int local_hist_r[256] = {0};
  int local_hist_g[256] = {0};
  int local_hist_b[256] = {0};

  // 第一阶段：本地累积（无原子操作）
  for(int pix = start; pix < end; pix++) {
    local_hist_r[input->r[pix]]++;
    local_hist_g[input->g[pix]]++;
    local_hist_b[input->b[pix]]++;
  }

  // 第二阶段：批量更新全局直方图（减少原子操作次数）
  for(int i = 0; i < 256; i++) {
    if(local_hist_r[i] > 0) {
      hist_r[i].value.fetch_add(local_hist_r[i], std::memory_order_relaxed);
    }
    if(local_hist_g[i] > 0) {
      hist_g[i].value.fetch_add(local_hist_g[i], std::memory_order_relaxed);
    }
    if(local_hist_b[i] > 0) {
      hist_b[i].value.fetch_add(local_hist_b[i], std::memory_order_relaxed);
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
		&input.r, &input.g, &input.b)) {
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

    // 优化3: 使用 new[] 正确构造原子对象
    AlignedAtomic *atomic_hist_r = new AlignedAtomic[256];
    AlignedAtomic *atomic_hist_g = new AlignedAtomic[256];
    AlignedAtomic *atomic_hist_b = new AlignedAtomic[256];

    pthread_t thread_ids[threads];
    struct index *idx = (struct index *) malloc(sizeof(struct index) * threads);
    int N = input.xsize * input.ysize;

    for (int i = 0; i < threads; i++) {
      idx[i].input = &input;
      idx[i].hist_r = atomic_hist_r;
      idx[i].hist_g = atomic_hist_g;
      idx[i].hist_b = atomic_hist_b;
      idx[i].start = N*i/threads;
      idx[i].end = N*(i+1)/threads;
      pthread_create(&thread_ids[i], NULL, lockfree_histogram, (void *) (idx+i));
    }
    
    for (int i = 0; i < threads; i++) {
      pthread_join(thread_ids[i], NULL);
    }

    // 读取结果
    for(int i = 0; i < 256; i++) {
      hist_r[i] = atomic_hist_r[i].value.load(std::memory_order_relaxed);
      hist_g[i] = atomic_hist_g[i].value.load(std::memory_order_relaxed);
      hist_b[i] = atomic_hist_b[i].value.load(std::memory_order_relaxed);
    }

    t.stop();

    FILE *out = fopen(output_file, "w");
    if(out) {
      print_histogram(out, hist_r, input.maxrgb);
      print_histogram(out, hist_g, input. maxrgb);
      print_histogram(out, hist_b, input.maxrgb);
      fclose(out);
    } else {
      fprintf(stderr, "Unable to output!\n");
    }
    
    printf("Time: %llu ns\n", t.duration());
    
    // 清理内存
    delete[] atomic_hist_r;
    delete[] atomic_hist_g;
    delete[] atomic_hist_b;
    free(hist_r);
    free(hist_g);
    free(hist_b);
    free(idx);
  }
  
  return 0;
}