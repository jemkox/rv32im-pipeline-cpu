// Deliberately broken file used to test hdl_lint itself.
// Every problem below must be reported; see run_tests.ps1 for the expected counts.
module bad (
    input  logic       clk,
    input  logic       reset,
    input  logic [3:0] a,
    output logic [3:0] y,
    output logic [3:0] z
);
    logic [3:0] q;

    assign y = a;

    always_comb begin
        y = a + 4'b0105;                 // invalid digit; y also driven by the assign above
    end

    always_ff @(posedge clk or posedge reset) begin
        if (posedge reset) begin         // edge used as a condition
            q = 4'd0;                    // blocking assignment in always_ff
        end else begin
            q <= a;
        end
    end

    always_ff begin                      // no event control
        z <= q;
    end

    always_comb begin
        z <= a;                          // nonblocking in always_comb; z also driven above
        case (a)
            4'd0:    z = 4'd1;
        endcase                          // no default
    end

    wire [3:0] w = 4'd20;                // does not fit in 4 bits
endmodule
