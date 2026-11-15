; ModuleID = '/Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/adi/adi.c'
source_filename = "/Users/aleilei/Documents/PerciLLM/20260114/Code-Transfer-Bench/adi/adi.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx26.0.0"

@.str = private unnamed_addr constant [7 x i8] c"%.17g\0A\00", align 1

; Function Attrs: nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  %1 = tail call dereferenceable_or_null(8000000) ptr @calloc(i64 noundef 1000000, i64 noundef 8) #5
  %2 = tail call dereferenceable_or_null(8000000) ptr @calloc(i64 noundef 1000000, i64 noundef 8) #5
  %3 = tail call dereferenceable_or_null(8000000) ptr @calloc(i64 noundef 1000000, i64 noundef 8) #5
  %4 = tail call dereferenceable_or_null(8000000) ptr @calloc(i64 noundef 1000000, i64 noundef 8) #5
  br label %5

5:                                                ; preds = %41, %0
  %6 = phi i64 [ 0, %0 ], [ %42, %41 ]
  %7 = mul nuw nsw i64 %6, 8000
  %8 = getelementptr i8, ptr %1, i64 %7
  %9 = add nuw nsw i64 %6, 1000
  %10 = insertelement <2 x i64> poison, i64 %9, i64 0
  %11 = shufflevector <2 x i64> %10, <2 x i64> poison, <2 x i32> zeroinitializer
  br label %12

12:                                               ; preds = %12, %5
  %13 = phi i64 [ 0, %5 ], [ %38, %12 ]
  %14 = phi <2 x i64> [ <i64 0, i64 1>, %5 ], [ %39, %12 ]
  %15 = add <2 x i64> %14, <i64 2, i64 2>
  %16 = add <2 x i64> %14, <i64 4, i64 4>
  %17 = add <2 x i64> %14, <i64 6, i64 6>
  %18 = sub nuw nsw <2 x i64> %11, %14
  %19 = sub nuw nsw <2 x i64> %11, %15
  %20 = sub nuw nsw <2 x i64> %11, %16
  %21 = sub nuw nsw <2 x i64> %11, %17
  %22 = trunc <2 x i64> %18 to <2 x i32>
  %23 = trunc <2 x i64> %19 to <2 x i32>
  %24 = trunc <2 x i64> %20 to <2 x i32>
  %25 = trunc <2 x i64> %21 to <2 x i32>
  %26 = uitofp <2 x i32> %22 to <2 x double>
  %27 = uitofp <2 x i32> %23 to <2 x double>
  %28 = uitofp <2 x i32> %24 to <2 x double>
  %29 = uitofp <2 x i32> %25 to <2 x double>
  %30 = fdiv <2 x double> %26, <double 1.000000e+03, double 1.000000e+03>
  %31 = fdiv <2 x double> %27, <double 1.000000e+03, double 1.000000e+03>
  %32 = fdiv <2 x double> %28, <double 1.000000e+03, double 1.000000e+03>
  %33 = fdiv <2 x double> %29, <double 1.000000e+03, double 1.000000e+03>
  %34 = getelementptr double, ptr %8, i64 %13
  %35 = getelementptr i8, ptr %34, i64 16
  %36 = getelementptr i8, ptr %34, i64 32
  %37 = getelementptr i8, ptr %34, i64 48
  store <2 x double> %30, ptr %34, align 8, !tbaa !6
  store <2 x double> %31, ptr %35, align 8, !tbaa !6
  store <2 x double> %32, ptr %36, align 8, !tbaa !6
  store <2 x double> %33, ptr %37, align 8, !tbaa !6
  %38 = add nuw i64 %13, 8
  %39 = add <2 x i64> %14, <i64 8, i64 8>
  %40 = icmp eq i64 %38, 1000
  br i1 %40, label %41, label %12, !llvm.loop !10

41:                                               ; preds = %12
  %42 = add nuw nsw i64 %6, 1
  %43 = icmp eq i64 %42, 1000
  br i1 %43, label %44, label %5, !llvm.loop !14

44:                                               ; preds = %41
  %45 = getelementptr i8, ptr %2, i64 -8000
  %46 = getelementptr inbounds i8, ptr %2, i64 8000
  %47 = getelementptr i8, ptr %1, i64 15992
  br label %48

48:                                               ; preds = %165, %44
  %49 = phi i32 [ 1, %44 ], [ %166, %165 ]
  br label %50

50:                                               ; preds = %104, %48
  %51 = phi i64 [ %107, %104 ], [ 0, %48 ]
  %52 = phi i64 [ %105, %104 ], [ 1, %48 ]
  %53 = mul nuw nsw i64 %51, 8000
  %54 = add i64 %53, 8000
  %55 = getelementptr i8, ptr %4, i64 %54
  %56 = getelementptr i8, ptr %3, i64 %54
  %57 = getelementptr inbounds double, ptr %2, i64 %52
  store double 1.000000e+00, ptr %57, align 8, !tbaa !6
  %58 = mul nuw nsw i64 %52, 1000
  %59 = getelementptr inbounds double, ptr %3, i64 %58
  store double 0.000000e+00, ptr %59, align 8, !tbaa !6
  %60 = getelementptr inbounds double, ptr %4, i64 %58
  store double 1.000000e+00, ptr %60, align 8, !tbaa !6
  %61 = getelementptr double, ptr %1, i64 %52
  %62 = load double, ptr %55, align 8
  %63 = load double, ptr %56, align 8
  br label %64

64:                                               ; preds = %64, %50
  %65 = phi double [ %63, %50 ], [ %69, %64 ]
  %66 = phi double [ %62, %50 ], [ %83, %64 ]
  %67 = phi i64 [ 1, %50 ], [ %85, %64 ]
  %68 = tail call double @llvm.fmuladd.f64(double %65, double 0xC09F400000000001, double 0x40AF420000000001)
  %69 = fdiv double 0x409F400000000001, %68
  %70 = add nuw nsw i64 %67, %58
  %71 = getelementptr inbounds double, ptr %3, i64 %70
  store double %69, ptr %71, align 8, !tbaa !6
  %72 = mul nuw nsw i64 %67, 8000
  %73 = getelementptr i8, ptr %61, i64 %72
  %74 = getelementptr i8, ptr %73, i64 -8
  %75 = load double, ptr %74, align 8, !tbaa !6
  %76 = load double, ptr %73, align 8, !tbaa !6
  %77 = fmul double %76, 0xC09F3C0000000001
  %78 = tail call double @llvm.fmuladd.f64(double %75, double 0x408F400000000001, double %77)
  %79 = getelementptr inbounds i8, ptr %73, i64 8
  %80 = load double, ptr %79, align 8, !tbaa !6
  %81 = tail call double @llvm.fmuladd.f64(double %80, double 0x408F400000000001, double %78)
  %82 = tail call double @llvm.fmuladd.f64(double %66, double 0x409F400000000001, double %81)
  %83 = fdiv double %82, %68
  %84 = getelementptr inbounds double, ptr %4, i64 %70
  store double %83, ptr %84, align 8, !tbaa !6
  %85 = add nuw nsw i64 %67, 1
  %86 = icmp eq i64 %85, 999
  br i1 %86, label %87, label %64, !llvm.loop !15

87:                                               ; preds = %64
  %88 = getelementptr inbounds i8, ptr %57, i64 7992000
  store double 1.000000e+00, ptr %88, align 8, !tbaa !6
  %89 = getelementptr i8, ptr %57, i64 8000
  br label %90

90:                                               ; preds = %90, %87
  %91 = phi i64 [ 998, %87 ], [ %102, %90 ]
  %92 = add nuw nsw i64 %91, %58
  %93 = getelementptr inbounds double, ptr %3, i64 %92
  %94 = load double, ptr %93, align 8, !tbaa !6
  %95 = mul nuw nsw i64 %91, 8000
  %96 = getelementptr i8, ptr %89, i64 %95
  %97 = load double, ptr %96, align 8, !tbaa !6
  %98 = getelementptr inbounds double, ptr %4, i64 %92
  %99 = load double, ptr %98, align 8, !tbaa !6
  %100 = tail call double @llvm.fmuladd.f64(double %94, double %97, double %99)
  %101 = getelementptr inbounds i8, ptr %57, i64 %95
  store double %100, ptr %101, align 8, !tbaa !6
  %102 = add nsw i64 %91, -1
  %103 = icmp ugt i64 %91, 1
  br i1 %103, label %90, label %104, !llvm.loop !16

104:                                              ; preds = %90
  %105 = add nuw nsw i64 %52, 1
  %106 = icmp eq i64 %105, 999
  %107 = add i64 %51, 1
  br i1 %106, label %108, label %50, !llvm.loop !17

108:                                              ; preds = %104, %161
  %109 = phi i64 [ %164, %161 ], [ 0, %104 ]
  %110 = phi i64 [ %162, %161 ], [ 1, %104 ]
  %111 = mul nuw nsw i64 %109, 8000
  %112 = getelementptr i8, ptr %47, i64 %111
  %113 = add i64 %111, 8000
  %114 = getelementptr i8, ptr %4, i64 %113
  %115 = getelementptr i8, ptr %3, i64 %113
  %116 = mul nuw nsw i64 %110, 1000
  %117 = getelementptr inbounds double, ptr %1, i64 %116
  store double 1.000000e+00, ptr %117, align 8, !tbaa !6
  %118 = getelementptr inbounds double, ptr %3, i64 %116
  store double 0.000000e+00, ptr %118, align 8, !tbaa !6
  %119 = getelementptr inbounds double, ptr %4, i64 %116
  store double 1.000000e+00, ptr %119, align 8, !tbaa !6
  %120 = getelementptr double, ptr %45, i64 %116
  %121 = getelementptr inbounds double, ptr %46, i64 %116
  %122 = load double, ptr %114, align 8
  %123 = load double, ptr %115, align 8
  br label %124

124:                                              ; preds = %124, %108
  %125 = phi double [ %123, %108 ], [ %129, %124 ]
  %126 = phi double [ %122, %108 ], [ %142, %124 ]
  %127 = phi i64 [ 1, %108 ], [ %144, %124 ]
  %128 = tail call double @llvm.fmuladd.f64(double %125, double 0xC08F400000000001, double 0x409F440000000001)
  %129 = fdiv double 0x408F400000000001, %128
  %130 = add nuw nsw i64 %127, %116
  %131 = getelementptr inbounds double, ptr %3, i64 %130
  store double %129, ptr %131, align 8, !tbaa !6
  %132 = getelementptr double, ptr %120, i64 %127
  %133 = load double, ptr %132, align 8, !tbaa !6
  %134 = getelementptr inbounds double, ptr %2, i64 %130
  %135 = load double, ptr %134, align 8, !tbaa !6
  %136 = fmul double %135, 0xC0AF3E0000000001
  %137 = tail call double @llvm.fmuladd.f64(double %133, double 0x409F400000000001, double %136)
  %138 = getelementptr inbounds double, ptr %121, i64 %127
  %139 = load double, ptr %138, align 8, !tbaa !6
  %140 = tail call double @llvm.fmuladd.f64(double %139, double 0x409F400000000001, double %137)
  %141 = tail call double @llvm.fmuladd.f64(double %126, double 0x408F400000000001, double %140)
  %142 = fdiv double %141, %128
  %143 = getelementptr inbounds double, ptr %4, i64 %130
  store double %142, ptr %143, align 8, !tbaa !6
  %144 = add nuw nsw i64 %127, 1
  %145 = icmp eq i64 %144, 999
  br i1 %145, label %146, label %124, !llvm.loop !18

146:                                              ; preds = %124
  %147 = getelementptr inbounds i8, ptr %117, i64 7992
  store double 1.000000e+00, ptr %147, align 8, !tbaa !6
  %148 = load double, ptr %112, align 8
  br label %149

149:                                              ; preds = %149, %146
  %150 = phi double [ %148, %146 ], [ %157, %149 ]
  %151 = phi i64 [ 998, %146 ], [ %159, %149 ]
  %152 = add nuw nsw i64 %151, %116
  %153 = getelementptr inbounds double, ptr %3, i64 %152
  %154 = load double, ptr %153, align 8, !tbaa !6
  %155 = getelementptr inbounds double, ptr %4, i64 %152
  %156 = load double, ptr %155, align 8, !tbaa !6
  %157 = tail call double @llvm.fmuladd.f64(double %154, double %150, double %156)
  %158 = getelementptr inbounds double, ptr %1, i64 %152
  store double %157, ptr %158, align 8, !tbaa !6
  %159 = add nsw i64 %151, -1
  %160 = icmp ugt i64 %151, 1
  br i1 %160, label %149, label %161, !llvm.loop !19

161:                                              ; preds = %149
  %162 = add nuw nsw i64 %110, 1
  %163 = icmp eq i64 %162, 999
  %164 = add i64 %109, 1
  br i1 %163, label %165, label %108, !llvm.loop !20

165:                                              ; preds = %161
  %166 = add nuw nsw i32 %49, 1
  %167 = icmp eq i32 %166, 501
  br i1 %167, label %168, label %48, !llvm.loop !21

168:                                              ; preds = %165
  %169 = load double, ptr %1, align 8, !tbaa !6
  %170 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, double noundef %169)
  tail call void @free(ptr noundef nonnull %1)
  tail call void @free(ptr noundef %2)
  tail call void @free(ptr noundef nonnull %3)
  tail call void @free(ptr noundef nonnull %4)
  ret i32 0
}

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite)
declare noalias noundef ptr @calloc(i64 noundef, i64 noundef) local_unnamed_addr #1

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare void @free(ptr allocptr nocapture noundef) local_unnamed_addr #2

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fmuladd.f64(double, double, double) #3

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #4

attributes #0 = { nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #1 = { mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #2 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #3 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #4 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "probe-stack"="__chkstk_darwin" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+bti,+ccdp,+ccidx,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8.5a,+v8a,+zcm,+zcz" }
attributes #5 = { allocsize(0,1) }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 2]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Apple clang version 17.0.0 (clang-1700.6.3.2)"}
!6 = !{!7, !7, i64 0}
!7 = !{!"double", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
!10 = distinct !{!10, !11, !12, !13}
!11 = !{!"llvm.loop.mustprogress"}
!12 = !{!"llvm.loop.isvectorized", i32 1}
!13 = !{!"llvm.loop.unroll.runtime.disable"}
!14 = distinct !{!14, !11}
!15 = distinct !{!15, !11}
!16 = distinct !{!16, !11}
!17 = distinct !{!17, !11}
!18 = distinct !{!18, !11}
!19 = distinct !{!19, !11}
!20 = distinct !{!20, !11}
!21 = distinct !{!21, !11}
