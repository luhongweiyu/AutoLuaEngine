print("about to trigger an expected error")

local text, err = file.read("/path/not/exist.txt")
if text ~= nil then
    error("unexpected file.read success")
end

print("expected file.read error =", err)

error("expected lua runtime error")
