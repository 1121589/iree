# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from absl import app
from iree.tf.support import tf_test_utils
import numpy as np
import tensorflow.compat.v2 as tf


class ControlFlowModule(tf.Module):

  def __init__(self):
    pass

  @tf.function(input_signature=[tf.TensorSpec([], tf.float32)])
  def collatz(self, a):
    i = 0.
    while a > 1.:
      i = i + 1.
      if (a % 2.) > 0.:
        a = 3. * a + 1.
      else:
        a = a / 2.
    return i


class ControlFlowTest(tf_test_utils.TracedModuleTestCase):

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self._modules = tf_test_utils.compile_tf_module(ControlFlowModule)

  def test_short_sequence(self):

    def short_sequence(module):
      input_array = np.array(9., dtype=np.float32)
      module.collatz(input_array)

    self.compare_backends(short_sequence, self._modules)

  def test_long_sequence(self):

    def long_sequence(module):
      input_array = np.array(178., dtype=np.float32)
      module.collatz(input_array)

    self.compare_backends(long_sequence, self._modules)


def main(argv):
  del argv  # Unused
  if hasattr(tf, 'enable_v2_behavior'):
    tf.enable_v2_behavior()
  tf.test.main()


if __name__ == '__main__':
  app.run(main)
