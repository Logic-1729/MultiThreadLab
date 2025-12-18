#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <cassert>
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

// 优化1: 缓存行对齐的直方图结构
struct alignas(64) AlignedHistogram {
  int r[256];
  int g[256];
  int b[256];
  char padding[64];  // 确保不同线程的结构在不同缓存行
};

struct index {
  struct img *input;
  AlignedHistogram *private_hist;
  int start;
  int end;
};

void* private_histogram(void *thread) {
  struct index *idx = (struct index *) thread;
  struct img *input = idx->input;
  int *hist_r = idx->private_hist->r;
  int *hist_g = idx->private_hist->g;
  int *hist_b = idx->private_hist->b;
  int start = idx->start;
  int end = idx->end;

  for(int pix = start; pix < end; pix++) {
    hist_r[input->r[pix]]++;
    hist_g[input->g[pix]]++;
    hist_b[input->b[pix]]++;
  }
  return NULL;
}

// 优化2: 并行合并
struct merge_index {
  AlignedHistogram *private_hists;
  int *hist_r;
  int *hist_g;
  int *hist_b;
  int num_threads;
  int start_bucket;
  int end_bucket;
};

void* parallel_merge(void *arg) {
  struct merge_index *midx = (struct merge_index *) arg;
  
  for(int j = midx->start_bucket; j < midx->end_bucket; j++) {
    int sum_r = 0, sum_g = 0, sum_b = 0;
    for(int i = 0; i < midx->num_threads; i++) {
      sum_r += midx->private_hists[i].r[j];
      sum_g += midx->private_hists[i].g[j];
      sum_b += midx->private_hists[i].b[j];
    }
    midx->hist_r[j] = sum_r;
    midx->hist_g[j] = sum_g;
    midx->hist_b[j] = sum_b;
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

    // 优化1: 使用对齐的结构体数组
    AlignedHistogram *private_hists = new AlignedHistogram[threads]();

    pthread_t thread_ids[threads];
    struct index *idx = (struct index *) malloc(sizeof(struct index) * threads);
    int N = input.xsize * input.ysize;

    // 计算阶段
    for (int i = 0; i < threads; i++) {
      idx[i].input = &input;
      idx[i].private_hist = &private_hists[i];
      idx[i].start = N*i/threads;
      idx[i].end = N*(i+1)/threads;
      pthread_create(&thread_ids[i], NULL, private_histogram, (void *) (idx+i));
    }
    
    for (int i = 0; i < threads; i++) {
      pthread_join(thread_ids[i], NULL);
    }

    // 优化2: 并行合并（仅当线程数较多时）
    if(threads >= 4) {
      int merge_threads = (threads > 4) ? 4 : threads;
      pthread_t merge_ids[merge_threads];
      struct merge_index *midx = (struct merge_index *) 
        malloc(sizeof(struct merge_index) * merge_threads);
      
      int buckets_per_thread = 256 / merge_threads;
      for(int i = 0; i < merge_threads; i++) {
        midx[i].private_hists = private_hists;
        midx[i].hist_r = hist_r;
        midx[i].hist_g = hist_g;
        midx[i].hist_b = hist_b;
        midx[i]. num_threads = threads;
        midx[i]. start_bucket = i * buckets_per_thread;
        midx[i].end_bucket = (i == merge_threads - 1) ? 256 : (i + 1) * buckets_per_thread;
        pthread_create(&merge_ids[i], NULL, parallel_merge, (void *) &midx[i]);
      }
      
      for(int i = 0; i < merge_threads; i++) {
        pthread_join(merge_ids[i], NULL);
      }
      free(midx);
    } else {
      // 串行合并（线程少时更快）
      for (int i = 0; i < threads; i++) {
        for (int j = 0; j <= input.maxrgb; j++) {
          hist_r[j] += private_hists[i]. r[j];
          hist_g[j] += private_hists[i].g[j];
          hist_b[j] += private_hists[i]. b[j];
        }
      }
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
    
    printf("Time: %llu ns\n", t.duration());
    
    // 优化3: 正确释放内存
    delete[] private_hists;
    free(hist_r);
    free(hist_g);
    free(hist_b);
    free(idx);
  }
  
  return 0;
}