fn merge(arr: ptr, left: int, mid: int, right: int)
    let tmp: ptr = alloc(128)
    let i: int = left
    let j: int = mid + 1
    let k: int = 0

    while i <= mid
        if j > right
            tmp[k] = arr[i]
            i = i + 1
            k = k + 1
        else
            let a: int = arr[i]
            let b: int = arr[j]
            if a <= b
                tmp[k] = arr[i]
                i = i + 1
            else
                tmp[k] = arr[j]
                j = j + 1
            end
            k = k + 1
        end
    end

    while j <= right
        tmp[k] = arr[j]
        j = j + 1
        k = k + 1
    end

    let idx: int = left
    let t: int = 0
    while idx <= right
        arr[idx] = tmp[t]
        idx = idx + 1
        t = t + 1
    end
end

fn merge_sort(arr: ptr, left: int, right: int)
    if left < right
        let mid: int = (left + right) / 2
        merge_sort(arr, left, mid)
        merge_sort(arr, mid + 1, right)
        merge(arr, left, mid, right)
    end
end

let buf: ptr = alloc(64)
buf[0] = 64
buf[1] = 34
buf[2] = 25
buf[3] = 12
buf[4] = 22
buf[5] = 11
buf[6] = 90
buf[7] = 1

print("before:")
for i = 0 to 7
    print(buf[i])
end

merge_sort(buf, 0, 7)

print("after:")
for i = 0 to 7
    print(buf[i])
end

free(buf, 64)
