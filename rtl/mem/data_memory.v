// data_memory.v - 256-word data RAM with byte / half-word / word access
//
// funct3 is the load/store funct3 field:
//   loads : 000 lb   001 lh   010 lw   100 lbu   101 lhu
//   stores: 000 sb   001 sh   010 sw
// Addresses are word indexed by addr[9:2]. Byte accesses use addr[1:0],
// half-word accesses use addr[1] (addr[0] is ignored), word accesses ignore
// addr[1:0]. Synchronous write, combinational read. Starts zeroed.
module data_memory (
    input  logic        clk,
    input  logic [31:0] addr,
    input  logic [31:0] wdata,
    input  logic        we,
    input  logic        re,
    input  logic [2:0]  funct3,
    output logic [31:0] rdata
);

    logic [31:0] mem [0:255];

    initial begin
        for (int i = 0; i < 256; i = i + 1) begin
            mem[i] = 32'd0;
        end
    end

    wire [7:0]  idx  = addr[9:2];
    wire [1:0]  boff = addr[1:0];
    wire [31:0] word = mem[idx];

    // ---- store: merge the written bytes into the existing word
    wire [31:0] sb_word = (boff == 2'd0) ? {word[31:8],  wdata[7:0]} :
                          (boff == 2'd1) ? {word[31:16], wdata[7:0], word[7:0]} :
                          (boff == 2'd2) ? {word[31:24], wdata[7:0], word[15:0]} :
                                           {wdata[7:0],  word[23:0]};
    wire [31:0] sh_word = addr[1] ? {wdata[15:0], word[15:0]} : {word[31:16], wdata[15:0]};
    wire [31:0] new_word = (funct3[1:0] == 2'b00) ? sb_word :
                           (funct3[1:0] == 2'b01) ? sh_word : wdata;

    always_ff @(posedge clk) begin
        if (we) begin
            mem[idx] <= new_word;
        end
    end

    // ---- load: pick the bytes and extend
    wire [7:0]  byte_val = (boff == 2'd0) ? word[7:0] :
                           (boff == 2'd1) ? word[15:8] :
                           (boff == 2'd2) ? word[23:16] : word[31:24];
    wire [15:0] half_val = addr[1] ? word[31:16] : word[15:0];

    wire [31:0] lb_val  = {{24{byte_val[7]}}, byte_val};
    wire [31:0] lh_val  = {{16{half_val[15]}}, half_val};
    wire [31:0] lbu_val = {24'd0, byte_val};
    wire [31:0] lhu_val = {16'd0, half_val};

    logic [31:0] ext;
    always_comb begin
        case (funct3)
            3'b000:  ext = lb_val;      // lb
            3'b001:  ext = lh_val;      // lh
            3'b100:  ext = lbu_val;     // lbu
            3'b101:  ext = lhu_val;     // lhu
            default: ext = word;        // lw
        endcase
    end

    assign rdata = re ? ext : 32'd0;

endmodule
