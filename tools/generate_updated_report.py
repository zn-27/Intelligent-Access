#!/usr/bin/env python3
"""Generate the latest research-progress report from the supplied DOCX.

The script keeps the original package, styles, figures, headers and footers,
updates paragraphs made obsolete by the latest code, and inserts a detailed
section describing the predictive intelligent-access algorithm and the
distributed IBSS beacon mechanism.
"""

from __future__ import annotations

import copy
import os
import tempfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
from xml.etree import ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "附件1_研究进展报告_原版备份.docx"
OUTPUT = ROOT / "附件1_研究进展报告_最新版_智能感知与分布式无中心.docx"

NS = {
    "wpc": "http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas",
    "mc": "http://schemas.openxmlformats.org/markup-compatibility/2006",
    "o": "urn:schemas-microsoft-com:office:office",
    "r": "http://schemas.openxmlformats.org/officeDocument/2006/relationships",
    "m": "http://schemas.openxmlformats.org/officeDocument/2006/math",
    "v": "urn:schemas-microsoft-com:vml",
    "wp14": "http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing",
    "wp": "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing",
    "w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main",
    "w14": "http://schemas.microsoft.com/office/word/2010/wordml",
    "w10": "urn:schemas-microsoft-com:office:word",
    "w15": "http://schemas.microsoft.com/office/word/2012/wordml",
    "wpg": "http://schemas.microsoft.com/office/word/2010/wordprocessingGroup",
    "wpi": "http://schemas.microsoft.com/office/word/2010/wordprocessingInk",
    "wne": "http://schemas.microsoft.com/office/word/2006/wordml",
    "wps": "http://schemas.microsoft.com/office/word/2010/wordprocessingShape",
    "wpsCustomData": "http://www.wps.cn/officeDocument/2013/wpsCustomData",
}
for prefix, uri in NS.items():
    ET.register_namespace(prefix, uri)

W = f"{{{NS['w']}}}"
M = f"{{{NS['m']}}}"
W14 = f"{{{NS['w14']}}}"
XML_SPACE = "{http://www.w3.org/XML/1998/namespace}space"


def text_of(element: ET.Element) -> str:
    return "".join(node.text or "" for node in element.iter(W + "t"))


def strip_generated_ids(element: ET.Element) -> None:
    for node in element.iter():
        node.attrib.pop(W14 + "paraId", None)
        node.attrib.pop(W14 + "textId", None)


def first_run_properties(paragraph: ET.Element) -> ET.Element | None:
    for run in paragraph.findall(W + "r"):
        rpr = run.find(W + "rPr")
        if rpr is not None:
            return copy.deepcopy(rpr)
    ppr = paragraph.find(W + "pPr")
    if ppr is not None:
        rpr = ppr.find(W + "rPr")
        if rpr is not None:
            return copy.deepcopy(rpr)
    return None


def set_paragraph_text(paragraph: ET.Element, text: str) -> None:
    rpr = first_run_properties(paragraph)
    for child in list(paragraph):
        if child.tag != W + "pPr":
            paragraph.remove(child)
    run = ET.SubElement(paragraph, W + "r")
    if rpr is not None:
        run.append(rpr)
    t = ET.SubElement(run, W + "t")
    if text.startswith(" ") or text.endswith(" "):
        t.set(XML_SPACE, "preserve")
    t.text = text


def clone_paragraph(template: ET.Element, text: str) -> ET.Element:
    paragraph = copy.deepcopy(template)
    strip_generated_ids(paragraph)
    set_paragraph_text(paragraph, text)
    return paragraph


def set_cell_text(cell: ET.Element, text: str) -> None:
    paragraphs = cell.findall(W + "p")
    if not paragraphs:
        paragraph = ET.SubElement(cell, W + "p")
    else:
        paragraph = paragraphs[0]
        for extra in paragraphs[1:]:
            cell.remove(extra)
    set_paragraph_text(paragraph, text)


def clone_table_with_rows(
    template: ET.Element, header: tuple[str, ...], rows: list[tuple[str, ...]]
) -> ET.Element:
    table = copy.deepcopy(template)
    strip_generated_ids(table)
    tr_list = table.findall(W + "tr")
    header_template = tr_list[0]
    data_template = tr_list[1]
    for tr in tr_list:
        table.remove(tr)

    new_header = copy.deepcopy(header_template)
    for cell, value in zip(new_header.findall(W + "tc"), header):
        set_cell_text(cell, value)
    table.append(new_header)

    for row in rows:
        tr = copy.deepcopy(data_template)
        for cell, value in zip(tr.findall(W + "tc"), row):
            set_cell_text(cell, value)
        table.append(tr)
    return table


def clone_equation_table(template: ET.Element, formula: str, number: int) -> ET.Element:
    table = copy.deepcopy(template)
    strip_generated_ids(table)
    row = table.find(W + "tr")
    cells = row.findall(W + "tc")
    set_cell_text(cells[0], "")
    set_cell_text(cells[2], f"（{number}）")

    center = cells[1]
    paragraphs = center.findall(W + "p")
    paragraph = paragraphs[0]
    for extra in paragraphs[1:]:
        center.remove(extra)
    for child in list(paragraph):
        if child.tag != W + "pPr":
            paragraph.remove(child)
    math_para = ET.SubElement(paragraph, M + "oMathPara")
    math = ET.SubElement(math_para, M + "oMath")
    math_run = ET.SubElement(math, M + "r")
    math_rpr = ET.SubElement(math_run, M + "rPr")
    math_style = ET.SubElement(math_rpr, M + "sty")
    math_style.set(M + "val", "p")
    math_text = ET.SubElement(math_run, M + "t")
    math_text.text = formula
    return table


def find_paragraph(root: ET.Element, exact: str) -> ET.Element:
    for paragraph in root.iter(W + "p"):
        if text_of(paragraph) == exact:
            return paragraph
    raise KeyError(f"Paragraph not found: {exact[:80]}")


def find_paragraph_start(root: ET.Element, prefix: str) -> ET.Element:
    for paragraph in root.iter(W + "p"):
        if text_of(paragraph).startswith(prefix):
            return paragraph
    raise KeyError(f"Paragraph prefix not found: {prefix}")


def replace_exact(root: ET.Element, old: str, new: str) -> None:
    matches = [paragraph for paragraph in root.iter(W + "p") if text_of(paragraph) == old]
    if not matches:
        raise KeyError(f"Paragraph not found: {old[:80]}")
    for paragraph in matches:
        set_paragraph_text(paragraph, new)


def build_new_section(
    root: ET.Element, parameter_table_template: ET.Element, equation_template: ET.Element
) -> list[ET.Element]:
    h2 = find_paragraph(root, "2.2.3 NS-3仿真场景、验证结果与工作小结")
    h3 = find_paragraph(root, "2.2.3.1 基于协同智能感知的灵巧重构技术仿真验证")
    normal = find_paragraph_start(root, "本节集中保留原报告中的仿真环境")
    step = find_paragraph(root, "（1）初始入网阶段（域 A，基础设施主导）")
    caption = find_paragraph(root, "表11 第二节点仿真实验参数与验证指标")

    out: list[ET.Element] = []

    def p(text: str) -> None:
        out.append(clone_paragraph(normal, text))

    def subsection(title: str) -> None:
        out.append(clone_paragraph(h3, title))

    def item(title: str) -> None:
        out.append(clone_paragraph(step, title))

    def equation(formula: str, number: int) -> None:
        out.append(clone_equation_table(equation_template, formula, number))

    out.append(
        clone_paragraph(
            h2,
            "2.2.4 最新版代码新增：移动性感知预测型智能接入与分布式无中心发现",
        )
    )
    p(
        "在前述协同感知、临机接入和灵巧重构框架基础上，最新版代码新增独立的智能接入算法模块"
        "IntelligentAccessAlgorithm，并将域C无中心接入面的发现机制由单一网关周期发送应用层UDP伪信标，"
        "升级为多个IBSS节点共同参与的原生IEEE 802.11 MAC管理信标竞争。前者负责对候选网络进行时间平滑、"
        "移动趋势预测、多属性效用评估和自适应切换保护；后者消除网络发现对唯一信标发送者的依赖。"
        "BlindConnectApp负责感知数据采集、地址协议和切换执行，智能接入算法负责纯决策，二者形成"
        "“观测—预测—评价—保护—执行—验证”的闭环。"
    )

    subsection("2.2.4.1 名词解释与软件对象边界")
    item("（1）观测量（Observation）")
    p(
        "观测量表示某一时刻对候选网络的原始测量，包含网络类型、逻辑域、SSID、网关、信道频点、"
        "接收信号功率、噪声功率、到网关跳数、归一化负载、最小剩余能量、节点数、安全能力位图、"
        "网关可达性、地址服务可用性和观测时间。接收信号功率与噪声功率的单位均为dBm；"
        "代码中的历史字段snr为兼容命名，进入新算法后按signalDbm解释，并由signalDbm−noiseDbm计算实际信噪比。"
    )
    item("（2）网络键（NetworkKey）与候选网络（Candidate）")
    p(
        "网络键由网络类型、逻辑域标识和网络标识三元组确定，用于合并同一网络在不同时间、不同发送节点产生的观测；"
        "网关地址和信道属于可变属性，不参与网络身份比较。候选网络是在观测量基础上增加指数平滑信号、"
        "指数平滑信噪比、预测信号、信号变化率、观测年龄、样本数和综合得分后的状态对象。"
    )
    item("（3）指数加权移动平均（EWMA）")
    p(
        "EWMA（Exponentially Weighted Moving Average，指数加权移动平均）用于抑制小尺度衰落、"
        "扫描瞬态和随机噪声。它对最新样本赋予权重α，对历史平滑值赋予权重1−α；α越大，算法对突变越敏感，"
        "α越小，输出越平稳。最新版仿真设置α=0.30。"
    )
    item("（4）时间触发门限、迟滞门限与乒乓切换")
    p(
        "时间触发门限TTT（Time To Trigger）要求目标网络持续优于当前网络达到规定时长后方可切换；"
        "迟滞门限H要求目标得分至少比当前得分高出H。二者与连续优胜次数、8 s最小驻留时间共同抑制"
        "边界区域内的AP—Ad-Hoc往返切换，即所谓乒乓切换。"
    )
    item("（5）目标信标传输时刻与分布式信标竞争")
    p(
        "TBTT（Target Beacon Transmission Time，目标信标传输时刻）是每个Beacon周期的共同时间基准。"
        "在分布式无中心域中，每个启用CollaborativeBeaconGeneration的AdhocWifiMac节点都具有信标生成资格，"
        "并在TBTT后独立选择随机退避时隙；节点若先收到同SSID的有效Beacon，则取消本周期尚未发送的Beacon。"
        "因此，信标发送权由各周期的局部竞争决定，而不永久绑定到某一中心节点。"
    )

    subsection("2.2.4.2 多源感知、候选融合与移动趋势预测")
    item("步骤1：双接口并行采集")
    p(
        "终端STA接口通过ReceiveStaBeacon接收A、B等基础设施域的标准AP Beacon；IBSS/Ad-Hoc接口通过"
        "ReceiveAdhocBeacon接收域C原生管理Beacon。两类接收路径统一生成ScannedNodeInfo，再转换为"
        "IntelligentAccessAlgorithm::Observation。非业务承载接口保持控制监听，从而在当前数据通路工作时"
        "持续获得备选网络状态。"
    )
    item("步骤2：同网络观测融合")
    p(
        "EvaluateAndSwitch在每个评价周期内按NetworkKey对观测分组；同一网络可能由多个IBSS对等节点通告，"
        "算法保留其中接收信号最强的观测作为本周期代表值，再调用Update写入候选历史。该处理把“多个发送节点”"
        "与“一个逻辑接入网络”区分开，避免同一域被重复计数。候选最大存活时间为6 s，超过该时间未更新的条目"
        "由Purge删除。"
    )
    item("步骤3：信号与信噪比的EWMA平滑")
    p("设第i个候选网络在离散时刻k的接收信号样本为Pᵢ(k)，则平滑信号按式（22）更新：")
    equation("P̄ᵢ(k)=αPᵢ(k)+(1−α)P̄ᵢ(k−1)", 22)
    p("若噪声样本为Nᵢ(k)，瞬时信噪比为Pᵢ(k)−Nᵢ(k)，则平滑信噪比按式（23）更新：")
    equation("γ̄ᵢ(k)=α[Pᵢ(k)−Nᵢ(k)]+(1−α)γ̄ᵢ(k−1)", 23)
    p(
        "首次观测直接作为平滑初值。每个候选保存最近5 s且最多32个平滑信号样本，"
        "既限制存储开销，又为短期运动趋势估计提供时间序列。"
    )
    item("步骤4：相关性加权的线性趋势估计")
    p(
        "算法以样本时间tⱼ和平滑信号yⱼ=P̄ᵢ(tⱼ)执行一元线性回归。原始斜率bᵢ按式（24）计算，"
        "并利用相关系数ρᵢ降低非单调衰落和跳频抖动对预测的影响："
    )
    equation("bᵢ=[nΣtⱼyⱼ−(Σtⱼ)(Σyⱼ)]/[nΣtⱼ²−(Σtⱼ)²]", 24)
    equation("ρᵢ=[nΣtⱼyⱼ−(Σtⱼ)(Σyⱼ)]/√{[nΣtⱼ²−(Σtⱼ)²][nΣyⱼ²−(Σyⱼ)²]}", 25)
    p(
        "有效趋势vᵢ=bᵢ|ρᵢ|，并截断到[−3,3] dB/s。预测时域Tₚ=1.5 s，"
        "预测信号按式（26）得到："
    )
    equation("P̂ᵢ=P̄ᵢ+clip(bᵢ|ρᵢ|,−vₘₐₓ,vₘₐₓ)Tₚ", 26)
    p(
        "当候选信号单调增强时，正趋势提高其预期收益；当信号远离且持续衰减时，负趋势降低其得分。"
        "这种预测在终端尚未到达覆盖边界前即可识别正在衰弱的当前链路和正在接近的目标链路。"
    )

    subsection("2.2.4.3 多属性归一化与综合效用计算")
    p(
        "评分前先执行资格过滤：若平滑信号和预测信号均低于−88 dBm，则候选不可用；"
        "若启用安全能力、网关可达或地址服务等强制条件，缺少相应能力的候选同样被剔除。"
        "通过资格过滤后，将不同量纲的感知指标映射到[0,1]。"
    )
    p("预测融合信号、信号质量归一化和信噪比归一化分别按式（27）—（29）计算：")
    equation("P̃ᵢ=0.4P̄ᵢ+0.6P̂ᵢ", 27)
    equation("qᴿᵢ=clip[(P̃ᵢ+90)/60,0,1]", 28)
    equation("qˢᵢ=clip{[γ̄ᵢ+(P̂ᵢ−P̄ᵢ)]/40,0,1}", 29)
    p("链路质量综合量由接收功率和信噪比两部分组成：")
    equation("qᵢ=0.6qᴿᵢ+0.4qˢᵢ", 30)
    p(
        "设hᵢ为截断到8跳后归一化的跳数，lᵢ为负载，eᵢ为最小剩余能量，sᵢ为六类安全能力"
        "（完整性、保密性、双向认证、抗重放、前向安全和分布式认证）中已声明能力的比例，"
        "τᵢ为归一化信号趋势，cᵢ为切换代价，aᵢ/L为观测年龄相对于候选生命周期的比例，"
        "则综合效用按式（31）计算："
    )
    equation(
        "Uᵢ=wq qᵢ−wh hᵢ−wl lᵢ+we eᵢ+ws sᵢ+wt τᵢ−wc cᵢ−wf(aᵢ/L)",
        31,
    )
    p(
        "同网络切换代价为0；同域且同类型但网络标识不同的代价为0.20；跨域但网络类型相同为0.55；"
        "基础设施与Ad-Hoc之间跨类型切换为1.00。该分段代价可写为式（32）："
    )
    equation("cᵢ∈{0,0.20,0.55,1.00}", 32)
    p(
        "最新版train4-new.cc对仿真对象设置wq=0.55、wh=0.10、wl=0.08、we=0.08、"
        "ws=0.08、wt=0.04、wc=0.03、wf=0.06。正项表示收益，负项表示代价，"
        "因此各权重不要求简单相加为1。"
    )

    out.append(clone_paragraph(caption, "表12 最新版智能接入与分布式信标关键参数"))
    out.append(
        clone_paragraph(
            normal,
            "如表12所示，参数值均来自最新版intelligent-access-algorithm.cc默认属性及"
            "train4-new.cc场景覆盖值；其中场景覆盖值优先于类默认值。",
        )
    )
    parameter_rows = [
        ("链路质量权重wq", "0.55", "预测接收功率与预测信噪比的综合收益。"),
        ("跳数惩罚权重wh", "0.10", "到网关跳数越多，得分越低。"),
        ("负载惩罚权重wl", "0.08", "归一化域负载越高，得分越低。"),
        ("能量收益权重we", "0.08", "域内最小剩余能量越高，得分越高。"),
        ("安全能力权重ws", "0.08", "按六类安全能力位的覆盖比例计分。"),
        ("趋势收益权重wt", "0.04", "奖励接近中的网络，惩罚远离中的网络。"),
        ("切换代价权重wc", "0.03", "抑制不必要的跨域、跨模式切换。"),
        ("新鲜度惩罚权重wf", "0.06", "观测越接近失效，惩罚越大。"),
        ("EWMA系数α", "0.30", "平衡最新样本响应速度与历史平滑能力。"),
        ("基础迟滞H₀", "0.015", "自适应下限为0.005。"),
        ("基础TTT", "1.0 s", "自适应范围为0.25—2.0 s。"),
        ("预测时域Tₚ", "1.5 s", "用于计算预测接收信号。"),
        ("趋势窗口", "5.0 s", "最多保存32个平滑样本。"),
        ("趋势截断vₘₐₓ", "3.0 dB/s", "限制异常样本导致的过大预测斜率。"),
        ("候选生命周期L", "6.0 s", "超龄候选从候选表中删除。"),
        ("紧急预测门限", "−82 dBm", "低于该值时缩短保护时间并降低迟滞。"),
        ("连续优胜次数", "2次", "目标至少连续两轮优于当前网络。"),
        ("最小驻留时间", "8.0 s", "非紧急情况下限制连续切换频率。"),
        ("协同Beacon周期", "102.4 ms", "域C原生IBSS管理信标周期。"),
        ("退避范围与时隙", "0—15槽，9 μs/槽", "每个TBTT重新随机竞争发送权。"),
    ]
    out.append(
        clone_table_with_rows(
            parameter_table_template, ("参数", "取值", "工程含义"), parameter_rows
        )
    )

    subsection("2.2.4.4 移动性自适应迟滞、TTT与软切换执行")
    p(
        "固定迟滞和固定TTT无法同时兼顾高速离开时的切换及时性与覆盖重叠区内的稳定性。"
        "新版算法分别定义当前网络衰减量d、目标网络接近量a和目标网络远离量r，"
        "三者均由信号趋势除以最大允许趋势并截断到[0,1]："
    )
    equation("d=clip(−vcur/vₘₐₓ), a=clip(vtar/vₘₐₓ), r=clip(−vtar/vₘₐₓ)", 33)
    p(
        "当前网络快速衰减或目标网络正在接近时，应降低迟滞和TTT；目标网络正在远离时，应提高保护强度。"
        "有效迟滞按式（34）计算；若当前网络缺失或其预测信号不高于−82 dBm，则判定为紧急状态，"
        "迟滞尺度再乘0.35。"
    )
    equation("Hₑff=max[Hₘᵢₙ,H₀·max(0.1,1−0.60d−0.25a+0.40r)]", 34)
    p(
        "有效TTT按式（35）计算，并截断在0.25—2.0 s；紧急状态下尺度再乘0.30："
    )
    equation("TTTₑff=clip{TTT₀·max(0.1,1−0.65d−0.25a+0.75r),0.25,2.0}", 35)
    p(
        "完整判决条件为：目标候选通过资格过滤；目标得分大于当前得分与有效迟滞之和；"
        "目标连续优胜不少于2次；持续优胜时间达到有效TTT；非紧急情况下距上次切换不少于8 s。"
        "首次接入或当前接口尚未取得有效地址时不执行上述迟滞等待，直接选择最佳可用候选。"
    )
    p(
        "执行阶段采用准备—提交—宽限三阶段软切换。BeginHandover保存旧域、旧接口、旧地址、旧网关和旧数据面；"
        "候选接口通过IP_REQUEST/IP_OFFER完成地址配置后，CommitHandover原子更新默认路由和业务套接字绑定；"
        "旧控制路径继续保留2 s宽限时间。若候选配置失败，AbortHandover恢复旧上下文并重置切换保护器。"
        "该过程属于make-before-break（先建后拆），其目的在于把链路发现、地址配置与业务路径提交解耦。"
    )

    subsection("2.2.4.5 无中心域的分布式原生Beacon机制")
    p(
        "旧实现由GATEWAY角色周期性发送TYPE:IBSS_BEACON应用层UDP伪信标，网络发现依赖单一发送节点。"
        "最新版在AdhocWifiMac中新增CollaborativeBeaconGeneration、CollaborativeBeaconInterval、"
        "CollaborativeDomainId、CollaborativeRole、CollaborativeGateway、CollaborativeHops和"
        "CollaborativeSecurityCapabilities等属性，并为协同管理帧设置独立Txop。"
        "域C的sw3网关和3个静止对等节点均启用原生Beacon生成，Legacy UDP回退开关"
        "UseLegacyUdpBeacon显式设置为false。"
    )
    item("步骤1：共同时间基准")
    p(
        "各参与节点以102.4 ms为Beacon标称周期，根据当前仿真时刻计算下一TBTT。该周期对应IEEE 802.11"
        "常用的100 TU，其中1 TU=1024 μs。"
    )
    item("步骤2：每周期独立随机退避")
    p(
        "节点i在第m个TBTT重新从0—15中均匀抽取整数退避槽Uᵢ⁽ᵐ⁾，时隙σ=9 μs，"
        "其计划发送时刻按式（36）确定："
    )
    equation("tᵢ⁽ᵐ⁾=TBTTₘ+Uᵢ⁽ᵐ⁾σ,  Uᵢ⁽ᵐ⁾∼Uniform{0,…,15}", 36)
    item("步骤3：原生管理帧构造")
    p(
        "SendCollaborativeBeacon构造WIFI_MAC_MGT_BEACON帧，目的地址为广播地址，发送地址为本节点MAC，"
        "BSSID为域共享BSSID；MgtBeaconHeader携带SSID、支持速率、Beacon周期和IBSS能力位。"
        "由此，终端通过MonitorSnifferRx在物理/MAC接收路径上感知真实管理帧，而非解析应用层字符串模拟信标。"
    )
    item("步骤4：接收抑制与下一周期重入")
    p(
        "AdhocWifiMac::Receive收到同SSID的有效Beacon后，取消本节点当前周期的待发送事件，"
        "清空协同Beacon队列并重新调度下一TBTT。理想无冲突条件下，本周期发送者可表示为式（37）："
    )
    equation("i* = arg minᵢ Uᵢ⁽ᵐ⁾", 37)
    p(
        "因此，任一参与节点均可能成为某一周期的Beacon发送者；发送资格在后续周期重新竞争。"
        "若多个节点选择同一最小退避槽，仍由底层信道接入和物理接收结果处理竞争，"
        "报告不将该机制表述为强一致性的全局领导者选举。"
    )
    item("步骤5：角色与网关信息恢复")
    p(
        "管理Beacon用于分布式发现，仿真接收端再根据已注册的信标源设备读取CollaborativeRole、"
        "CollaborativeGateway和CollaborativeHops等属性：sw3角色为gateway、跳数为0；"
        "其余域C静止节点角色为peer、跳数为1。多个发送节点对应同一Adhoc-C逻辑网络，"
        "候选融合后由智能接入算法统一评价。当前元数据通过仿真对象映射恢复，尚未编码为标准Beacon信息元素；"
        "这是后续与真实设备互操作时需要补充的工程项。"
    )
    item("步骤6：分布式发现与网关服务解耦")
    p(
        "分布式Beacon机制取消的是“只有网关能够公告网络”的单点依赖，但网关仍提供IP_REQUEST/IP_OFFER"
        "地址池服务和跨域出口，SDN控制器仍负责ARP/主机视图与跨域流表。准确地说，当前实现已完成"
        "无中心接入面的分布式发现和协同通告，而不是取消所有网关及控制器功能。该边界保证报告中的"
        "“分布式无中心”与代码实现一致。"
    )

    subsection("2.2.4.6 代码落点与最新版运行记录验证")
    p(
        "新增算法主体位于contrib/mymodule/model/intelligent-access-algorithm.h/.cc；"
        "BlindConnectApp通过SetAccessAlgorithm注入算法对象，并在EvaluateAndSwitch中完成观测融合、"
        "周期评价和切换调用；src/wifi/model/adhoc-wifi-mac.h/.cc实现分布式管理Beacon的生成、"
        "随机退避和接收抑制；scratch/train4-new.cc配置域C参与节点、场景参数、信标源映射和运行日志。"
        "最新版工程已通过waf增量构建，mymodule、wifi、ofswitch13等相关模块构建成功。"
    )
    p(
        "join_events.csv记录了本次最新版仿真中的完整控制链。4.500 s首次选择域A基础设施网络，"
        "得分为0.417505；30.000 s选择域C Adhoc-C，得分为0.558877，预测信号为−40.572 dBm，"
        "趋势为1.58856 dB/s，有效迟滞为0.0122082，有效TTT为0.809404 s；"
        "从HANDOVER_PREPARE到HANDOVER_COMMIT用时0.502104 s。44.500 s选择域B基础设施网络，"
        "得分为0.473654，有效迟滞降至0.006、有效TTT降至0.35 s；受STA关联与地址请求调度影响，"
        "该次准备到提交用时3.002821 s。上述数值用于证明决策字段、动态门限、地址配置和软切换链路已实际贯通，"
        "不作为不同算法间统计显著性的性能结论。"
    )
    p(
        "同一运行记录中，域C共接收290条原生IBSS_BEACON_RX事件，发送源包括MAC地址尾号0c、0d、0e、0f"
        "四个不同节点，分别贡献69、62、78和81条记录；相邻接收事件间隔的中位数为0.1024 s，"
        "与配置的Beacon周期一致。多源发送记录直接表明域C网络通告已由单一网关伪信标转变为多节点分布式竞争。"
    )

    subsection("2.2.4.7 本次升级的技术结论")
    p(
        "最新版代码已将临机接入从静态、规则化的瞬时质量比较提升为移动性感知预测型多属性决策："
        "算法同时利用当前质量、短期趋势、跳数、负载、能量、安全能力、切换代价和观测新鲜度，"
        "并以自适应迟滞、TTT、连续优胜和最小驻留时间约束切换。与此同时，无中心域不再依赖网关应用层伪信标，"
        "而由多个AdhocWifiMac节点在共同TBTT上竞争产生原生管理Beacon。两项升级分别增强了"
        "“何时接入、接入何处”的决策质量和“无中心网络如何被持续发现”的抗单点能力，"
        "并通过双接口控制双活、数据单活及先建后拆软切换实现端到端协同。"
    )
    return out


def main() -> None:
    if not SOURCE.exists():
        raise SystemExit(f"Source report not found: {SOURCE}")

    with ZipFile(SOURCE, "r") as zin:
        document_bytes = zin.read("word/document.xml")
        root = ET.fromstring(document_bytes)

        replacements = {
            "协同感知信息建模、候选网络评分、接入切换、地址配置与控制器同步":
                "多源感知建模、移动趋势预测、多属性评分、自适应软切换与控制器同步",
            "多模态混合组网、可重构网元、Q-Learning重构、OpenFlow控制决策下发":
                "多模态混合组网、分布式无中心发现、Q-Learning重构与OpenFlow控制下发",
            "在实现层面，移动终端由临机接入应用模块BlindConnectApp承载入网控制逻辑：其终端角色ROLE_TERMINAL负责维护双无线接口——STA接口监听AP信标Beacon及基础设施接入状态，IBSS/Ad-Hoc接口监听自组织网络的伪信标与入网控制消息（入网地址请求IP_REQUEST、地址分配应答IP_OFFER等）。MAC（Media Access Control，媒体访问控制）地址用于链路层标识与地址池索引，并不单独作为跨域信任根。":
                "在实现层面，移动终端由临机接入应用模块BlindConnectApp承载入网控制逻辑：其终端角色ROLE_TERMINAL维护双无线接口，STA接口监听AP标准Beacon及基础设施接入状态，IBSS/Ad-Hoc接口监听由多个AdhocWifiMac节点竞争产生的原生IEEE 802.11管理Beacon及入网控制消息。MAC（Media Access Control，媒体访问控制）地址用于链路层标识与地址池索引，并不单独作为跨域信任根。",
            "候选网络综合评分由综合评分函数CalculateScore完成：该函数以时序平滑后的信噪比SNR作为链路质量基础输入，并融合至网关跳数（路由成本）、域内负载（拥塞惩罚）、节点剩余能量及安全能力等作为多维修正项。评估与切换决策函数EvaluateAndSwitch周期性对候选表排序，并结合最小驻留时间、同网络抑制、首次入网优先选择基础设施网络、IP配置状态等约束，降低短时波动导致的频繁切换。":
                "最新版将候选网络综合评分提取为独立的IntelligentAccessAlgorithm。该模块对接收信号和信噪比进行EWMA时序平滑，利用相关性加权线性回归预测短期信号趋势，并融合网关跳数、域内负载、节点剩余能量、安全能力、切换代价和观测新鲜度。EvaluateAndSwitch周期性执行候选融合与评价，再结合移动性自适应迟滞、自适应TTT、连续优胜次数和最小驻留时间确定是否切换，详细公式见2.2.4节。",
            "候选网络统一写入临机接入应用模块BlindConnectApp所维护的候选网络状态对象ScannedNodeInfo。该结构记录网络类型、SSID、接收信号质量、到网关跳数、域内负载、最小剩余能量、节点数量、安全能力和网关地址等字段。评估与切换决策函数EvaluateAndSwitch周期性对候选网络进行评分：当前程序中评分主要由接收信号质量、到网关跳数和域内能量共同决定，负载与安全能力字段已作为后续扩展项保留。为降低抖动，算法设置最小驻留时间、同网络抑制和首次入网优先选择基础设施网络等约束。":
                "候选网络首先写入BlindConnectApp维护的ScannedNodeInfo，再转换为IntelligentAccessAlgorithm::Observation。最新版评分已实际使用预测链路质量、跳数、负载、最小剩余能量、安全能力、信号趋势、切换代价和观测新鲜度，不再把负载与安全能力仅作为预留字段。算法设置候选生命周期、资格门限、移动性自适应迟滞、自适应TTT、连续优胜次数和8 s最小驻留时间，以降低短时衰落导致的乒乓切换。",
            "如图5展示了IBSS/Ad-Hoc模式下自组织接入与地址分配时序。首先，网关角色GATEWAY节点周期性发送类型为IBSS_BEACON的应用层网络通告（含SSID、网关IP、跳数、负载、能量与安全能力等）；然后，终端在评估选中自组织网络后，调用自组织侧地址请求函数RequestAdhocIp发送入网地址请求IP_REQUEST；最后，GATEWAY返回地址分配应答IP_OFFER，完成地址配置与控制器同步。该流程是应用层轻量地址分配，而非完整DHCP（Dynamic Host Configuration Protocol，动态主机配置协议），用以适配NS-3仿真中IBSS链路与OpenFlow数据平面的动态网关角色。IBSS/Ad-Hoc自组织接入对应无AP基础设施覆盖或基础设施接入质量不足时的临机接入方式。程序中交换节点sw3安装临机接入应用模块BlindConnectApp的网关角色ROLE_GATEWAY，面向域C维护10.100.3.10至10.100.3.50地址池。GATEWAY周期性发送IBSS_BEACON通告，字段包括SSID、网关IP、到网关跳数、域内节点数、平均负载、最小剩余能量和安全能力等信息。":
                "如图5展示了IBSS/Ad-Hoc模式下自组织接入与地址分配时序。最新版中，域C网关与对等节点均可在每个TBTT参与原生IEEE 802.11管理Beacon竞争；终端接收同SSID Beacon后，根据发送源对应的角色、网关、跳数和安全能力形成候选观测。终端在智能评价选中自组织网络后调用RequestAdhocIp发送IP_REQUEST，仍由GATEWAY地址服务返回IP_OFFER并完成控制器同步。由此，网络发现已从单一网关应用层伪信标升级为分布式原生MAC Beacon，而轻量地址分配仍由网关服务承担，二者在功能上相互解耦。",
            "终端通过Ad-Hoc无线网卡对象mobileEmDev接收IBSS网络通告后，将其加入候选网络表。若评估与切换决策函数EvaluateAndSwitch判定自组织网络适合作为当前业务承载网络，则终端调用自组织侧地址请求函数RequestAdhocIp发送类型为IP_REQUEST的入网地址请求。网关GATEWAY解析终端MAC并分配IBSS接入子网地址，随后返回类型为IP_OFFER的地址分配应答；终端再调用自组织地址消息处理函数HandleAdhocIpMessage与接口地址配置函数ConfigureIpOnInterface完成IBSS接口IPv4地址配置，并发送类型为IP_CONFIRM的地址确认。":
                "终端通过Ad-Hoc无线网卡对象mobileEmDev接收多个域C节点竞争产生的原生管理Beacon，按逻辑域和SSID合并为同一候选网络，并由智能接入算法执行时序平滑、趋势预测和多属性评价。若EvaluateAndSwitch判定自组织网络适合作为业务承载网络，则RequestAdhocIp向网关地址服务发送IP_REQUEST；GATEWAY分配IBSS子网地址并返回IP_OFFER，终端通过HandleAdhocIpMessage与ConfigureIpOnInterface完成配置并发送IP_CONFIRM。",
            "从阶段验证结果看，系统已能支撑基础设施模式、IBSS/Ad-Hoc自组织模式和SDN骨干多中心协同模式三类网络工作模式。后续工作的重点不是重新搭建仿真平台，而是在既有原型基础上把硬切换逻辑逐步收敛为应用层数据面休眠逻辑，并增加双地址维护、双控制通道保活、跨域端口一致映射和学习型重构策略。":
                "最新版验证结果表明，系统已支撑基础设施模式、分布式IBSS/Ad-Hoc自组织模式和SDN骨干多中心协同模式。终端双接口保持控制面监听，跨接口切换采用准备—提交—宽限三阶段软切换；智能接入模块已实现EWMA平滑、移动趋势预测、多属性评分、自适应迟滞与TTT，域C已实现多节点竞争产生原生MAC Beacon。具体实现参数和运行记录见2.2.4节。",
            "（一）当前原型仍包含部分硬切换逻辑，与双接口并行驻留目标存在差距。问题描述：主仿真脚本train4-new.cc和临机接入应用模块BlindConnectApp中仍可见接口关闭调用Ipv4::SetDown、清空SSID和关闭部分控制套接字等硬切换痕迹，与“控制面双活、数据面单活”目标不完全一致。解决措施：后续保留双接口UP状态与双地址配置，在BlindConnectApp应用层统一实现业务流门控、默认路由切换与套接字绑定切换，逐步替换硬切换路径。":
                "（一）同无线接口内跨AP切换仍受物理信道与关联过程约束。问题描述：最新版已经对STA—Ad-Hoc跨接口切换实现准备—提交—宽限三阶段先建后拆，但同一STA无线接口在跨AP或跨信道时不能同时保持两个物理关联，仍需执行快速重关联。解决措施：继续保留双接口控制监听和旧路径回退上下文，缩短AP关联与地址请求调度，并评估增加第二STA射频或多链路能力，以降低同接口跨AP切换时间。",
            "下一阶段将继续优化临机接入决策算法。以接收信号质量、跳数、负载、剩余能量、安全能力、切换代价和业务QoS（Quality of Service，服务质量）需求为状态输入，引入群体智能算法，把吞吐量、丢包率、时延、抖动和入网成功率纳入奖励函数，实现从规则驱动切换向学习驱动重构演进。":
                "下一阶段将继续优化临机接入决策算法。在现有移动性感知预测型多属性决策基础上，进一步标定负载、能量与安全能力的在线测量来源，引入业务QoS需求驱动的动态权重和置信度估计，并把吞吐量、丢包率、时延、抖动、入网成功率及切换中断时间纳入闭环评价，实现参数离线标定与在线自适应相结合。",
            "本阶段项目组按第二节点任务推进算法设计、仿真实现和成果归档。实现工作集中在train4-new.cc和BlindConnectApp两部分，覆盖双接口接入、地址分配协议、OpenFlow端口适配、控制器ARP同步和数据面休眠方案，期间经过多轮代码与设计迭代。文档方面同步维护了Markdown设计说明、SVG设计图、仿真参数表与本阶段报告，使设计、代码和汇报材料可相互对照。":
                "本阶段项目组按第二节点任务推进算法设计、仿真实现和成果归档。最新版实现工作覆盖train4-new.cc、BlindConnectApp、IntelligentAccessAlgorithm和AdhocWifiMac：分别承担场景配置与验证、双接口接入及地址协议、移动性预测多属性决策、分布式原生IBSS Beacon。相关修改同步覆盖mymodule构建配置、WiFi MAC模块和事件数据，已完成增量构建与运行记录核对。文档方面同步维护设计说明、仿真参数、公式推导与本阶段报告，使设计、代码和汇报材料可相互对照。",
            "对照第二节点合同约定任务和考核要求，承担单位已完成基于协同智能感知的临机接入算法原型设计与仿真实现，形成了基础设施模式、IBSS/Ad-Hoc自组织模式和SDN骨干多中心协同模式三类混合网络工作模式；完成了双无线接口并行驻留、双地址域维护、应用层数据面休眠、动态地址分配、控制器ARP表项注入和VirtualNetDevice数据平面适配的体系化设计；并基于BlindConnectApp和ofswitch13控制器完成了主要流程验证。本阶段研究工作能够支撑“支持混合中心工作模式，网络模式>=3种”的节点考核要求，阶段任务完成情况良好，后续将重点开展软休眠机制代码收敛、控制器端口映射增强和大规模性能验证。":
                "对照第二节点合同约定任务和考核要求，承担单位已完成基于协同智能感知的临机接入算法原型设计与仿真实现，形成基础设施模式、分布式IBSS/Ad-Hoc自组织模式和SDN骨干多中心协同模式三类混合网络工作模式；完成双无线接口并行驻留、动态地址分配、控制器ARP同步和VirtualNetDevice数据平面适配；新增移动性感知预测型多属性接入算法，实现EWMA平滑、趋势预测、综合效用、自适应迟滞与TTT；并将域C网络发现由单网关UDP伪信标升级为多AdhocWifiMac节点竞争产生的原生管理Beacon。现有结果达到“支持混合中心工作模式，网络模式不少于3种”的阶段考核要求。后续将重点完善Beacon扩展信息元素、在线指标测量、同接口跨AP快速切换和多次重复实验统计。",
        }
        for old, new in replacements.items():
            replace_exact(root, old, new)

        # Update the control-message table so the main body no longer describes
        # the disabled legacy UDP pseudo-beacon as the active discovery path.
        for table in root.iter(W + "tbl"):
            rows = table.findall(W + "tr")
            if not rows:
                continue
            header = [text_of(c) for c in rows[0].findall(W + "tc")]
            if header == ["控制消息", "发送方向", "主要字段", "含义"]:
                cells = rows[1].findall(W + "tc")
                values = (
                    "IEEE 802.11 IBSS Beacon",
                    "竞争获胜的任一IBSS节点到终端",
                    "SSID、Beacon周期、IBSS能力；角色/网关/跳数由仿真源映射恢复",
                    "以原生MAC管理帧分布式发布无中心域存在性，供终端发现和评分。",
                )
                for cell, value in zip(cells, values):
                    set_cell_text(cell, value)
                break

        tables = list(root.iter(W + "tbl"))
        parameter_table_template = next(
            table
            for table in tables
            if [text_of(c) for c in table.find(W + "tr").findall(W + "tc")]
            == ["项目", "设置", "说明"]
        )
        equation_template = next(
            table
            for table in reversed(tables)
            if len(table.findall(W + "tr")) == 1
            and text_of(table).strip().endswith("（21）")
        )

        body = root.find(W + "body")
        anchor = None
        for child in body:
            if child.tag == W + "p" and text_of(child) == "2.3管理工作情况":
                anchor = child
                break
        if anchor is None:
            raise KeyError("Insertion anchor 2.3管理工作情况 not found")
        insert_at = list(body).index(anchor)
        for element in build_new_section(root, parameter_table_template, equation_template):
            body.insert(insert_at, element)
            insert_at += 1

        new_document = ET.tostring(root, encoding="utf-8", xml_declaration=True)
        # Word accepts the generated XML without an explicit standalone flag.
        with tempfile.NamedTemporaryFile(
            prefix="updated-report-", suffix=".docx", dir=ROOT, delete=False
        ) as temp:
            temp_path = Path(temp.name)
        try:
            with ZipFile(temp_path, "w", ZIP_DEFLATED) as zout:
                for item in zin.infolist():
                    payload = (
                        new_document
                        if item.filename == "word/document.xml"
                        else zin.read(item.filename)
                    )
                    zout.writestr(item, payload)
            os.replace(temp_path, OUTPUT)
        finally:
            if temp_path.exists():
                temp_path.unlink()

    print(OUTPUT)


if __name__ == "__main__":
    main()
