import oneflow as flow
import numpy as np
import os


def test_lazy_input_output(test_case):
    flow.clear_default_session()
    flow.enable_eager_execution(False)

    @flow.global_function()
    def foo_job(input_def=flow.FixedTensorDef(shape=(2, 5))):
        var = flow.get_variable(
            name="var",
            shape=(2, 5),
            dtype=flow.float,
            initializer=flow.ones_initializer(),
        )
        output = var + input_def
        return output

    checkpoint = flow.train.CheckPoint()
    checkpoint.init()
    input = np.arange(10).reshape(2, 5).astype(np.single)
    ret = foo_job(input).get()
    output = input + np.ones(shape=(2, 5), dtype=np.single)
    test_case.assertTrue(np.array_equal(output, ret.numpy()))


def test_eager_output(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    @flow.global_function()
    def foo_job():
        x = flow.constant(1, shape=(2, 5), dtype=flow.float)
        return x

    ret = foo_job().get()
    test_case.assertTrue(
        np.array_equal(np.ones(shape=(2, 5), dtype=np.single), ret.numpy_list()[0])
    )


def test_eager_multi_output(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    @flow.global_function()
    def foo_job():
        x = flow.constant(1, shape=(2, 5), dtype=flow.float)
        y = flow.get_variable(
            name="var",
            shape=(64, 4),
            dtype=flow.float,
            initializer=flow.zeros_initializer(),
        )
        return x, y

    x, y = foo_job().get()
    test_case.assertTrue(
        np.array_equal(np.ones(shape=(2, 5), dtype=np.single), x.numpy_list()[0])
    )
    test_case.assertTrue(
        np.array_equal(np.zeros(shape=(64, 4), dtype=np.single), y.numpy())
    )


def test_eager_input(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    input = np.random.rand(2, 5).astype(np.single)
    output = np.maximum(input, 0)

    @flow.global_function()
    def foo_job(x_def=flow.MirroredTensorDef(shape=(2, 5), dtype=flow.float)):
        y = flow.math.relu(x_def)
        test_case.assertTrue(np.allclose(y.numpy(0), output))

    foo_job([input])


def test_eager_input_fixed(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    input = np.arange(10).astype(np.single)
    output = input + 1.0

    @flow.global_function()
    def foo_job(x_def=flow.FixedTensorDef(shape=(10,), dtype=flow.float)):
        y = x_def + flow.constant(1.0, shape=(1,), dtype=flow.float)
        test_case.assertTrue(np.allclose(y.numpy(0), output))

    foo_job(input)


def test_eager_multi_input(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    input_1 = np.random.rand(3, 4).astype(np.single)
    input_2 = np.array([2]).astype(np.single)
    output = input_1 * input_2

    @flow.global_function()
    def foo_job(
        x_def=flow.MirroredTensorDef(shape=(3, 4), dtype=flow.float),
        y_def=flow.MirroredTensorDef(shape=(1,), dtype=flow.float),
    ):
        y = x_def * y_def
        test_case.assertTrue(np.allclose(y.numpy(0), output))

    foo_job([input_1], [input_2])


def test_eager_input_output(test_case):

    flow.clear_default_session()
    flow.enable_eager_execution()

    input = np.random.rand(5, 4).astype(np.single)
    output = input * 2.0

    @flow.global_function()
    def foo_job(x_def=flow.MirroredTensorDef(shape=(5, 4), dtype=flow.float)):
        y = x_def * flow.constant(2.0, shape=(1,), dtype=flow.float)
        return y

    ret = foo_job([input]).get()
    test_case.assertTrue(np.allclose(output, ret.numpy_list()[0]))


# TODO: system op need manaully register blob_object in default_blob_register or bw_blob_register
# def test_eager_system_op(test_case):

#     flow.clear_default_session()
#     flow.enable_eager_execution()

#     input = np.random.rand(5, 4).astype(np.single)

#     @flow.global_function()
#     # def foo_job(x_def=flow.FixedTensorDef(shape=(5, 4), dtype=flow.float)):
#     def foo_job():
#         x = flow.constant(1, shape=(5, 4), dtype=flow.float)
#         y = flow.identity(x)
#         print([y.numpy(i) for i in range(y.numpy_size())])
#         # y = flow.identity(x_def)
#         # return y

#     foo_job()
#     # ret = foo_job(input).get()
#     # test_case.assertTrue(np.allclose(input, ret.numpy()))