<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration {
    public function up(): void
    {
        Schema::create('readings', function (Blueprint $t) {
            $t->id();
            $t->float('temperature');
            $t->float('humidity');
            $t->string('mode', 10);              // IDLE, MODE1, MODE2, MODE3, MANUAL
            $t->boolean('alert')->default(false);
            $t->timestamps();
            $t->index('created_at');
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('readings');
    }
};
