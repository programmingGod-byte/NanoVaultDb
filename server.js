const net = require("net");
const server = net.createServer((socket) => {
    console.log("Client connected");
    socket.on("data", (data) => {
        const message = data.toString();
        console.log("Received:");
        console.log(message);
    });
    socket.on("end", () => {
        console.log("Client disconnected");
    });
});
server.listen(8080, () => {
    console.log("TCP Server listening on port 8080");
});