// Copyright (c) 2017 Sony Corporation. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// slice.cpp

#include <nbla/array.hpp>
#include <nbla/function/slice.hpp>
#include <nbla/variable.hpp>

#include <cstring>

namespace nbla {

NBLA_REGISTER_FUNCTION_SOURCE(Slice, const vector<int> &, // start
                              const vector<int> &,        // stop
                              const vector<int> &);       // step

template <typename T>
void Slice<T>::setup_impl(const Variables &inputs, const Variables &outputs) {
  Shape_t shape_x = inputs[0]->shape();
  const int size = shape_x.size();
  NBLA_CHECK(stop_[0].size() == start_[0].size(), error_code::value,
             "size of stop and start must be same. "
             "Stop size: %d != start size: %d.",
             stop_[0].size(), start_[0].size());
  NBLA_CHECK(step_[0].size() == start_[0].size(), error_code::value,
             "size of step and start must be same. "
             "Step size: %d != start size: %d.",
             step_[0].size(), start_[0].size());

  for (int i = size - start_[0].size() - 1; i >= 0; --i) {
    start_[0].insert(start_[0].begin(), 0);
    stop_[0].insert(stop_[0].begin(), shape_x[i]);
    step_[0].insert(step_[0].begin(), 1);
  }

  NBLA_CHECK(start_[0].size() == size, error_code::value,
             "Size of start must be same as input size. "
             "Start size: %d != input size: %d.",
             start_[0].size(), size);
  NBLA_CHECK(stop_[0].size() == size, error_code::value,
             "size of stop must be same as input size. "
             "Stop size: %d != input size: %d.",
             stop_[0].size(), size);
  NBLA_CHECK(step_[0].size() == size, error_code::value,
             "size of step must be smaller than input size. "
             "Step size: %d != input size: %d.",
             step_[0].size(), size);

  Shape_t shape_y(size);
  for (int i = 0; i < size; i++) {
    NBLA_CHECK(0 <= start_[0][i] && start_[0][i] <= shape_x[i],
               error_code::value, "start[%d] must be between 0 and %d. "
                                  "start[%d]: %d.",
               i, shape_x[i], i, start_[0][i]);
    NBLA_CHECK(0 <= stop_[0][i] && stop_[0][i] <= shape_x[i], error_code::value,
               "stop[%d] must be between 0 and %d. "
               "stop[%d]: %d.",
               i, shape_x[i], i, stop_[0][i]);
    NBLA_CHECK(start_[0][i] <= stop_[0][i], error_code::value,
               "start must be smaller than stop. "
               "start[%d]: %d > stop[%d]: %d.",
               i, start_[0][i], i, stop_[0][i]);
    NBLA_CHECK(0 < step_[0][i], error_code::value,
               "step must be positive integer. step[%d]: %d.", step_[0][i]);
    const int size_i = int((stop_[0][i] - 1 - start_[0][i]) / step_[0][i]) + 1;
    shape_y[i] = size_i;
  }

  outputs[0]->reshape(shape_y, true);
}

template <typename T>
void Slice<T>::slice_forward_recursive(const Variable *inp, Variable *outp,
                                       const T *x, T *y, int x_offset,
                                       int y_offset, int dim,
                                       int &slice_index) {
  int current_x_offset = x_offset, current_y_offset = y_offset;
  const int x_stride = inp->strides()[dim] * step_[slice_index][dim];
  const int y_stride = outp->strides()[dim];
  current_x_offset += inp->strides()[dim] * start_[slice_index][dim];
  const int size = outp->shape()[dim];

  if (dim == inp->shape().size() - 1) {
    const T *current_x = x + current_x_offset;
    T *current_y = y + current_y_offset;
    if (x_stride == 1) {
      memcpy(current_y, current_x, sizeof(T) * size);
    } else {
      const T *end_x = current_x + size * x_stride;
      while (current_x != end_x) {
        *current_y = *current_x;
        current_x += x_stride;
        current_y += y_stride;
      }
    }
  } else {
    for (int i = 0; i < size; i++) {
      slice_forward_recursive(inp, outp, x, y, current_x_offset,
                              current_y_offset, dim + 1, slice_index);
      current_x_offset += x_stride;
      current_y_offset += y_stride;
      if (dim < base_axis_) {
        slice_index = (slice_index + 1) % start_.size();
      }
    }
  }
}

template <typename T>
void Slice<T>::slice_backward_recursive(Variable *outp, const Variable *inp,
                                        T *dx, const T *dy, int x_offset,
                                        int y_offset, int dim,
                                        int &slice_index) {
  int current_x_offset = x_offset, current_y_offset = y_offset;
  const int x_stride = outp->strides()[dim] * step_[slice_index][dim];
  const int y_stride = inp->strides()[dim];
  current_x_offset += outp->strides()[dim] * start_[slice_index][dim];
  const int size = inp->shape()[dim];

  if (dim == outp->shape().size() - 1) {
    T *current_dx = dx + current_x_offset;
    const T *current_dy = dy + current_y_offset;
    T *end_dx = current_dx + size * x_stride;
    while (current_dx != end_dx) {
      *current_dx += *current_dy;
      current_dx += x_stride;
      current_dy += y_stride;
    }
  } else {
    for (int i = 0; i < size; i++) {
      slice_backward_recursive(outp, inp, dx, dy, current_x_offset,
                               current_y_offset, dim + 1, slice_index);
      current_x_offset += x_stride;
      current_y_offset += y_stride;
      if (dim < base_axis_) {
        slice_index = (slice_index + 1) % start_.size();
      }
    }
  }
}

template <class T>
void Slice<T>::forward_impl(const Variables &inputs, const Variables &outputs) {
  const T *x = inputs[0]->get_data_pointer<T>(this->ctx_);
  T *y = outputs[0]->cast_data_and_get_pointer<T>(this->ctx_, true);

  int slice_index = 0;
  slice_forward_recursive(inputs[0], outputs[0], x, y, 0, 0, 0, slice_index);
}

template <class T>
void Slice<T>::backward_impl(const Variables &inputs, const Variables &outputs,
                             const vector<bool> &propagate_down,
                             const vector<bool> &accum) {
  if (!propagate_down[0]) {
    return;
  }
  if (!accum[0])
    inputs[0]->grad()->zero();

  const T *dy = outputs[0]->get_grad_pointer<T>(this->ctx_);
  T *dx = inputs[0]->cast_grad_and_get_pointer<T>(this->ctx_, false);

  int slice_index = 0;
  slice_backward_recursive(inputs[0], outputs[0], dx, dy, 0, 0, 0, slice_index);
}
}
