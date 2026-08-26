package com.uav.platform.common;

/**
 * REST API 的统一 JSON 返回格式。
 *
 * @param success 请求是否成功
 * @param message 给调用方看的结果说明
 * @param data 实际业务数据，类型由泛型 T 决定
 */
public record Result<T>(boolean success, String message, T data) {

    /** 创建带数据的成功响应。 */
    public static <T> Result<T> success(T data) {
        return new Result<>(true, "success", data);
    }

    /** 创建带自定义提示和数据的成功响应。 */
    public static <T> Result<T> success(String message, T data) {
        return new Result<>(true, message, data);
    }

    /** 创建不带业务数据的失败响应。 */
    public static <T> Result<T> failure(String message) {
        return new Result<>(false, message, null);
    }
}
