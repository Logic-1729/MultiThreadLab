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

struct index {
  struct img *input;
  std:: atomic<int> *hist_r;
  std::atomic<int> *hist_g;
  std::atomic<int> *hist_b;
  int start;
  int end;
};

void* lockfree_histogram(void * thread) {
  struct index *idx = (struct index *) thread;
  struct img *input = idx->input;
  std::atomic<int> *hist_r = idx->hist_r;
  std::atomic<int> *hist_g = idx->hist_g;
  std::atomic<int> *hist_b = idx->hist_b;
  int start = idx->start;
  int end = idx->end;

  for(int pix = start; pix < end; pix++) {
    hist_r[input->r[pix]].fetch_add(1, std::memory_order_relaxed);
    hist_g[input->g[pix]].fetch_add(1, std::memory_order_relaxed);
    hist_b[input->b[pix]].fetch_add(1, std::memory_order_relaxed);
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  if(argc != 4) {
    printf("Usage: %s input-file output-file threads\n", argv[0]);
    printf("       For single-threaded runs, pass threads = 1\n");
    exit(1);
  }
  
  char *output_file = argv[2];
  char *input_file = argv[1];
  int threads = atoi(argv[3]);

  struct img input;

  if(! ppmb_read(input_file, &input.xsize, &input.ysize, &input.maxrgb, 
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

    void *raw_r = calloc(input.maxrgb+1, sizeof(std::atomic<int>));
    void *raw_g = calloc(input.maxrgb+1, sizeof(std::atomic<int>));
    void *raw_b = calloc(input.maxrgb+1, sizeof(std::atomic<int>));
    
    std::atomic<int> *atomic_hist_r = static_cast<std::atomic<int>*>(raw_r);
    std::atomic<int> *atomic_hist_g = static_cast<std::atomic<int>*>(raw_g);
    std::atomic<int> *atomic_hist_b = static_cast<std::atomic<int>*>(raw_b);
    
    for(int i = 0; i <= input.maxrgb; i++) {
      new (&atomic_hist_r[i]) std::atomic<int>(0);
      new (&atomic_hist_g[i]) std::atomic<int>(0);
      new (&atomic_hist_b[i]) std::atomic<int>(0);
    }

    pthread_t thread_ids[threads];
    struct index *idx = (struct index *) malloc(sizeof(struct index) * threads);
    int N = input.xsize * input.ysize;

    for (int i = 0; i < threads; i++) {
      idx[i].input = &input;
      idx[i]. hist_r = atomic_hist_r;
      idx[i]. hist_g = atomic_hist_g;
      idx[i]. hist_b = atomic_hist_b;
      idx[i]. start = N*i/threads;
      idx[i]. end = N*(i+1)/threads;
      pthread_create(&thread_ids[i], NULL, lockfree_histogram, (void *) (idx+i));
    }
    
    for (int i = 0; i < threads; i++) {
      pthread_join(thread_ids[i], NULL);
    }

    for(int i = 0; i <= input.maxrgb; i++) {
        hist_r[i] = atomic_hist_r[i]. load(std::memory_order_relaxed);
        hist_g[i] = atomic_hist_g[i].load(std:: memory_order_relaxed);
        hist_b[i] = atomic_hist_b[i]. load(std::memory_order_relaxed);
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
    
    for(int i = 0; i <= input.maxrgb; i++) {
      atomic_hist_r[i].~atomic();
      atomic_hist_g[i].~atomic();
      atomic_hist_b[i].~atomic();
    }
    
    free(raw_r);
    free(raw_g);
    free(raw_b);
    free(hist_r);
    free(hist_g);
    free(hist_b);
    free(idx);
    free(input.r);
    free(input.g);
    free(input.b);
  }
  
  return 0;
}