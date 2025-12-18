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

struct index {
  struct img *input;
  int *hist_r;
  int *hist_g;
  int *hist_b;
  int start;
  int end;
};

void* private_histogram(void *thread) {
  struct index *idx = (struct index *) thread;
  struct img *input = idx->input;
  int *hist_r = idx->hist_r;
  int *hist_g = idx->hist_g;
  int *hist_b = idx->hist_b;
  int start = idx->start;
  int end = idx->end;

  for(int pix = start; pix < end; pix++) {
    hist_r[input->r[pix]] += 1;
    hist_g[input->g[pix]] += 1;
    hist_b[input->b[pix]] += 1;
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

    int **private_hist_r = (int **) malloc(threads * sizeof(int *));
    int **private_hist_g = (int **) malloc(threads * sizeof(int *));
    int **private_hist_b = (int **) malloc(threads * sizeof(int *));
    for(int i = 0; i < threads; i++) {
      private_hist_r[i] = (int *) calloc(input.maxrgb+1, sizeof(int));
      private_hist_g[i] = (int *) calloc(input.maxrgb+1, sizeof(int));
      private_hist_b[i] = (int *) calloc(input.maxrgb+1, sizeof(int));
    }

    pthread_t thread_ids[threads];
    struct index *idx = (struct index *) malloc(sizeof(struct index) * threads);
    int N = input.xsize * input.ysize;

    for (int i = 0; i < threads; i++) {
      idx[i].input = &input;
      idx[i].hist_r = private_hist_r[i];
      idx[i].hist_g = private_hist_g[i];
      idx[i].hist_b = private_hist_b[i];
      idx[i].start = N*i/threads;
      idx[i].end = N*(i+1)/threads;
      pthread_create(&thread_ids[i], NULL, private_histogram, (void *) (idx+i));
    }
    
    for (int i = 0; i < threads; i++) {
      pthread_join(thread_ids[i], NULL);
    }
    
    for (int i = 0; i < threads; i++) {
      for (int j = 0; j <= input.maxrgb; j++) {
        hist_r[j] += private_hist_r[i][j];
        hist_g[j] += private_hist_g[i][j];
        hist_b[j] += private_hist_b[i][j];
      }
    }
    
    t.stop();

    FILE *out = fopen(output_file, "w");
    if(out) {
      print_histogram(out, hist_r, input.maxrgb);
      print_histogram(out, hist_g, input.maxrgb);
      print_histogram(out, hist_b, input. maxrgb);
      fclose(out);
    } else {
      fprintf(stderr, "Unable to output!\n");
    }
    
    printf("Time: %llu ns\n", t.duration());
    
    for(int i = 0; i < threads; i++) {
      free(private_hist_r[i]);
      free(private_hist_g[i]); 
      free(private_hist_b[i]);
    }
    free(private_hist_r);
    free(private_hist_g);
    free(private_hist_b);
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